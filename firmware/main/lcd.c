#include "lcd.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "lcd";

#define PIN_CS   10
#define PIN_DC   46
#define PIN_SCK  12
#define PIN_MOSI 11
#define PIN_BL   45
#define LCD_HOST SPI2_HOST

static esp_lcd_panel_io_handle_t s_io;
static lcd_done_cb_t s_done_cb;
static void *s_done_ctx;

static bool on_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    (void)io; (void)ev; (void)ctx;
    if (s_done_cb) s_done_cb(s_done_ctx);
    return false;
}

static void cmd(uint8_t c, const uint8_t *d, size_t n)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, c, d, n));
}
#define CMD(c, ...) do { const uint8_t d_[] = {__VA_ARGS__}; cmd(c, d_, sizeof d_); } while (0)

// Landscape MADCTL values from the vendor table (reference/ILI9341V_Init.txt, directions 1 and 3):
// BGR | MV (swap rows/columns) | MX or MY. Portrait was 0x08 (proven on the bench).
#if CONFIG_JD_LCD_LANDSCAPE_B
#define MADCTL 0xA8
#else
#define MADCTL 0x68
#endif

// Vendor init (reference/ILI9341V_Init.txt). Inversion ON, BGR, landscape, no mirroring.
static void vendor_init(void)
{
    CMD(0xCF, 0x00, 0xC1, 0x30);
    CMD(0xED, 0x64, 0x03, 0x12, 0x81);
    CMD(0xE8, 0x85, 0x00, 0x78);
    CMD(0xCB, 0x39, 0x2C, 0x00, 0x34, 0x02);
    CMD(0xF7, 0x20);
    CMD(0xEA, 0x00, 0x00);
    CMD(0xC0, 0x13);
    CMD(0xC1, 0x13);
    CMD(0xC5, 0x22, 0x35);
    CMD(0xC7, 0xBD);
    CMD(0xB6, 0x0A, 0xA2);
    CMD(0x3A, 0x55);              // RGB565
    CMD(0xF6, 0x01, 0x30);
    CMD(0xB1, 0x00, 0x1B);
    CMD(0xF2, 0x00);
    CMD(0x26, 0x01);
    CMD(0xE0, 0x0F, 0x35, 0x31, 0x0B, 0x0E, 0x06, 0x49, 0xA7, 0x33, 0x07, 0x0F, 0x03, 0x0C, 0x0A, 0x00);
    CMD(0xE1, 0x00, 0x0A, 0x0F, 0x04, 0x11, 0x08, 0x36, 0x58, 0x4D, 0x07, 0x10, 0x0C, 0x32, 0x34, 0x0F);
    cmd(0x21, NULL, 0);           // inversion on
    CMD(0x36, MADCTL);            // MADCTL: BGR + landscape
}

esp_err_t lcd_init(int spi_hz, size_t max_transfer_bytes, lcd_done_cb_t done_cb, void *ctx)
{
    s_done_cb = done_cb;
    s_done_ctx = ctx;
    gpio_config_t bl = { .pin_bit_mask = 1ULL << PIN_BL, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&bl));
    gpio_set_level(PIN_BL, 0);

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SCK, .mosi_io_num = PIN_MOSI, .miso_io_num = -1,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = (int)max_transfer_bytes,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_CS, .dc_gpio_num = PIN_DC,
        .spi_mode = 0, .pclk_hz = spi_hz,
        .trans_queue_depth = 4,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .on_color_trans_done = on_done,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io));

    cmd(0x01, NULL, 0);           // software reset (reset pin is tied to EN)
    vTaskDelay(pdMS_TO_TICKS(150));
    vendor_init();
    cmd(0x11, NULL, 0);           // sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    cmd(0x29, NULL, 0);           // display on
    ESP_LOGI(TAG, "ILI9341V ready, SPI %d MHz", spi_hz / 1000000);
    return ESP_OK;
}

esp_err_t lcd_draw(int x1, int y1, int x2, int y2, const void *pixels)
{
    CMD(0x2A, x1 >> 8, x1 & 0xFF, x2 >> 8, x2 & 0xFF);
    CMD(0x2B, y1 >> 8, y1 & 0xFF, y2 >> 8, y2 & 0xFF);
    size_t bytes = (size_t)(x2 - x1 + 1) * (size_t)(y2 - y1 + 1) * 2;
    return esp_lcd_panel_io_tx_color(s_io, 0x2C, pixels, bytes);
}

void lcd_backlight(bool on) { gpio_set_level(PIN_BL, on); }
