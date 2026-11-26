#include "touch.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd.h"

static const char *TAG = "touch";

// The I2C bus is shared with the audio codec (0x18), which we never use.
#define PIN_SDA 16
#define PIN_SCL 15
#define PIN_RST 18
#define FT_ADDR 0x38

#define RAW_W 240 // the chip reports portrait coordinates: x 0..239, y 0..319
#define RAW_H 320

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bool                    s_was_down;

esp_err_t touch_init(void)
{
    gpio_config_t rst = { .pin_bit_mask = 1ULL << PIN_RST, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&rst));
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(300)); // FT6336 needs ~300 ms after reset

    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0, .sda_io_num = PIN_SDA, .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t e = i2c_new_master_bus(&bus, &s_bus);
    if (e != ESP_OK) return e;
    e = i2c_master_probe(s_bus, FT_ADDR, 100);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "no touch chip at 0x%02X (%s): running without touch", FT_ADDR, esp_err_to_name(e));
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        return e;
    }
    i2c_device_config_t dev = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = FT_ADDR, .scl_speed_hz = 400000 };
    e = i2c_master_bus_add_device(s_bus, &dev, &s_dev);
    if (e != ESP_OK) return e;
    uint8_t reg = 0xA3, chip = 0;
    i2c_master_transmit_receive(s_dev, &reg, 1, &chip, 1, 20);
    ESP_LOGI(TAG, "FT6336 ready (chip id 0x%02X)", chip);
    return ESP_OK;
}

bool touch_get(int *x, int *y)
{
    if (!s_dev) return false;
    uint8_t reg = 0x02, b[5]; // finger count, then the first finger: xh, xl, yh, yl
    if (i2c_master_transmit_receive(s_dev, &reg, 1, b, sizeof b, 20) != ESP_OK) return false;
    int n = b[0] & 0x0F;
    if (n < 1 || n > 2) { s_was_down = false; return false; }
    int rx = ((b[1] & 0x0F) << 8) | b[2];
    int ry = ((b[3] & 0x0F) << 8) | b[4];
    if (rx >= RAW_W) rx = RAW_W - 1;
    if (ry >= RAW_H) ry = RAW_H - 1;
    // Landscape A (MADCTL 0x68 = MV | MX) turns the portrait picture a quarter turn;
    // landscape B (0xA8) is the same turned 180 degrees.
#if CONFIG_JD_LCD_LANDSCAPE_B
    *x = LCD_W - 1 - ry;
    *y = rx;
#else
    *x = ry;
    *y = LCD_H - 1 - rx;
#endif
    if (!s_was_down) ESP_LOGI(TAG, "down at (%d,%d), raw (%d,%d)", *x, *y, rx, ry); // to check the mapping
    s_was_down = true;
    return true;
}
