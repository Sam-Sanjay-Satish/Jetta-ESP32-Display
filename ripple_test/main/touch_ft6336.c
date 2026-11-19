#include "touch_ft6336.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch";

#define PIN_SDA 16
#define PIN_SCL 15
#define PIN_RST 18
#define PIN_INT 17   // not used for touch (we poll); only read for diagnostics
#define FT_ADDR 0x38

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static touch_config_t s_cfg;

static esp_err_t read_reg(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 20);
}

static int scan_bus(i2c_master_bus_handle_t bus)
{
    int found = 0;
    for (int a = 0x08; a < 0x78; a++)
        if (i2c_master_probe(bus, a, 20) == ESP_OK) { ESP_LOGW(TAG, "  device at 0x%02X", a); found++; }
    return found;
}

esp_err_t touch_init(const touch_config_t *cfg)
{
    s_cfg = *cfg;

    gpio_config_t rst = { .pin_bit_mask = 1ULL << PIN_RST, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&rst));
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(300));   // FT6336 needs ~300 ms after reset

    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0, .sda_io_num = PIN_SDA, .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_bus));

    esp_err_t p = i2c_master_probe(s_bus, FT_ADDR, 100);
    ESP_LOGI(TAG, "probe 0x%02X: %s", FT_ADDR, esp_err_to_name(p));
    if (p != ESP_OK) {
        // Diagnostic: list everything that answers, with the normal pins and with SDA/SCL swapped.
        // Try the reset line floating, held low, and held high, each with a long wait.
        const char *names[] = { "floating", "low", "high" };
        for (int m = 0; m < 3; m++) {
            if (m == 0) gpio_set_direction(PIN_RST, GPIO_MODE_INPUT);
            else { gpio_set_direction(PIN_RST, GPIO_MODE_OUTPUT); gpio_set_level(PIN_RST, m == 2); }
            vTaskDelay(pdMS_TO_TICKS(600));
            ESP_LOGW(TAG, "RST %s: %d device(s)", names[m], scan_bus(s_bus));
        }
        gpio_set_direction(PIN_RST, GPIO_MODE_OUTPUT); gpio_set_level(PIN_RST, 1);
        int found = scan_bus(s_bus);
        ESP_LOGW(TAG, "I2C scan (SDA=%d SCL=%d): %d device(s)", PIN_SDA, PIN_SCL, found);
        i2c_del_master_bus(s_bus);
        i2c_master_bus_config_t sw = bus;
        sw.sda_io_num = PIN_SCL; sw.scl_io_num = PIN_SDA;
        if (i2c_new_master_bus(&sw, &s_bus) == ESP_OK) {
            found = scan_bus(s_bus);
            ESP_LOGW(TAG, "I2C scan (SDA=%d SCL=%d, swapped): %d device(s)", PIN_SCL, PIN_SDA, found);
            i2c_del_master_bus(s_bus);
        }
        s_bus = NULL;
        // Read the level of RST/INT: a line stuck low would explain a dead chip.
        gpio_set_direction(PIN_INT, GPIO_MODE_INPUT);
        ESP_LOGW(TAG, "RST(IO%d) out=1, INT(IO%d) reads %d", PIN_RST, PIN_INT, gpio_get_level(PIN_INT));
        return p;
    }

    i2c_device_config_t dev = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = FT_ADDR, .scl_speed_hz = 400000 };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev, &s_dev));

    uint8_t chip = 0, vendor = 0;
    read_reg(0xA3, &chip, 1);
    read_reg(0xA8, &vendor, 1);
    ESP_LOGI(TAG, "chip id 0x%02X, vendor id 0x%02X", chip, vendor);
    return ESP_OK;
}

int touch_read(touch_point_t out[2])
{
    if (!s_dev) return -1;
    uint8_t b[13];   // reg 0x02: count, then 6 bytes per finger (xh, xl, yh, yl, weight, area)
    if (read_reg(0x02, b, sizeof b) != ESP_OK) return -1;
    int n = b[0] & 0x0F;
    if (n > 2) n = (n == 0x0F) ? 0 : 2;
    for (int i = 0; i < n; i++) {
        const uint8_t *f = &b[1 + 6 * i];
        int x = ((f[0] & 0x0F) << 8) | f[1];
        int y = ((f[2] & 0x0F) << 8) | f[3];
        out[i].id = f[2] >> 4;
        out[i].raw_x = x;
        out[i].raw_y = y;
        if (s_cfg.swap_xy) { int t = x; x = y; y = t; }
        if (s_cfg.mirror_x) x = s_cfg.width - 1 - x;
        if (s_cfg.mirror_y) y = s_cfg.height - 1 - y;
        if (x < 0) x = 0;
        if (x >= s_cfg.width) x = s_cfg.width - 1;
        if (y < 0) y = 0;
        if (y >= s_cfg.height) y = s_cfg.height - 1;
        out[i].x = x;
        out[i].y = y;
    }
    return n;
}
