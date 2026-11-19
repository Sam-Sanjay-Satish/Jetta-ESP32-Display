#include "display_ili9341.h"
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "display";

#define PIN_CS   10
#define PIN_DC   46
#define PIN_SCK  12
#define PIN_MOSI 11
#define PIN_BL   45
// MISO (IO13) is not used: we never read from the panel.
#define LCD_HOST SPI2_HOST
#define NUM_CHUNKS 8   // frame is sent in 8 pieces (keeps every DMA transfer small)

static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_done;
static uint16_t *s_fb;
static int s_w, s_h;

static bool on_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

static void cmd(uint8_t c, const uint8_t *d, size_t n)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, c, d, n));
}
#define CMD(c, ...) do { const uint8_t d_[] = {__VA_ARGS__}; cmd(c, d_, sizeof d_); } while (0)

// Vendor init sequence (ILI9341V_Init.txt from lcdwiki.com), minus the
// settings that come from display_config_t (0x21/0x20 inversion, 0x36 MADCTL).
static void vendor_init(const display_config_t *cfg)
{
    CMD(0xCF, 0x00, 0xC1, 0x30);
    CMD(0xED, 0x64, 0x03, 0x12, 0x81);
    CMD(0xE8, 0x85, 0x00, 0x78);
    CMD(0xCB, 0x39, 0x2C, 0x00, 0x34, 0x02);
    CMD(0xF7, 0x20);
    CMD(0xEA, 0x00, 0x00);
    CMD(0xC0, 0x13);              // power control 1
    CMD(0xC1, 0x13);              // power control 2
    CMD(0xC5, 0x22, 0x35);        // VCOM 1
    CMD(0xC7, 0xBD);              // VCOM 2
    CMD(0xB6, 0x0A, 0xA2);        // display function control
    CMD(0x3A, 0x55);              // 16 bit/pixel (RGB565)
    CMD(0xF6, 0x01, 0x30);        // interface control
    CMD(0xB1, 0x00, 0x1B);        // frame rate
    CMD(0xF2, 0x00);              // 3-gamma off
    CMD(0x26, 0x01);              // gamma curve 1
    CMD(0xE0, 0x0F, 0x35, 0x31, 0x0B, 0x0E, 0x06, 0x49, 0xA7, 0x33, 0x07, 0x0F, 0x03, 0x0C, 0x0A, 0x00);
    CMD(0xE1, 0x00, 0x0A, 0x0F, 0x04, 0x11, 0x08, 0x36, 0x58, 0x4D, 0x07, 0x10, 0x0C, 0x32, 0x34, 0x0F);

    cmd(cfg->invert ? 0x21 : 0x20, NULL, 0);
    uint8_t madctl = (cfg->bgr ? 0x08 : 0) | (cfg->mirror_y ? 0x80 : 0) |
                     (cfg->mirror_x ? 0x40 : 0) | (cfg->swap_xy ? 0x20 : 0);
    CMD(0x36, madctl);
}

esp_err_t display_init(const display_config_t *cfg)
{
    s_w = cfg->swap_xy ? 320 : 240;
    s_h = cfg->swap_xy ? 240 : 320;
    size_t fb_bytes = (size_t)s_w * s_h * 2;

    s_fb = heap_caps_malloc(fb_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_fb) { ESP_LOGE(TAG, "no RAM for frame buffer (%u bytes)", (unsigned)fb_bytes); return ESP_ERR_NO_MEM; }
    memset(s_fb, 0, fb_bytes);
    s_done = xSemaphoreCreateCounting(NUM_CHUNKS, 0);

    gpio_config_t bl = { .pin_bit_mask = 1ULL << PIN_BL, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&bl));
    gpio_set_level(PIN_BL, 0);

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SCK, .mosi_io_num = PIN_MOSI, .miso_io_num = -1,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = (int)(fb_bytes / NUM_CHUNKS),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_CS, .dc_gpio_num = PIN_DC,
        .spi_mode = 0, .pclk_hz = cfg->spi_hz,
        .trans_queue_depth = NUM_CHUNKS + 2,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .on_color_trans_done = on_done,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io));

    // Reset is tied to the chip's EN pin (no GPIO), so use a software reset.
    cmd(0x01, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
    vendor_init(cfg);
    cmd(0x11, NULL, 0);                       // sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    cmd(0x29, NULL, 0);                       // display on
    vTaskDelay(pdMS_TO_TICKS(20));
    CMD(0x2A, 0, 0, (s_w - 1) >> 8, (s_w - 1) & 0xFF);   // column window = full width
    CMD(0x2B, 0, 0, (s_h - 1) >> 8, (s_h - 1) & 0xFF);   // row window = full height

    display_push();                            // black first, then backlight on
    gpio_set_level(PIN_BL, 1);
    ESP_LOGI(TAG, "ready %dx%d, SPI %d MHz, invert=%d bgr=%d swap=%d mx=%d my=%d", s_w, s_h,
             cfg->spi_hz / 1000000, cfg->invert, cfg->bgr, cfg->swap_xy, cfg->mirror_x, cfg->mirror_y);
    return ESP_OK;
}

int display_width(void)  { return s_w; }
int display_height(void) { return s_h; }
uint16_t *display_framebuffer(void) { return s_fb; }

esp_err_t display_push(void)
{
    size_t chunk = (size_t)s_w * s_h * 2 / NUM_CHUNKS;
    const uint8_t *p = (const uint8_t *)s_fb;
    for (int i = 0; i < NUM_CHUNKS; i++) {
        // 0x2C = start writing at the window origin, 0x3C = continue where we stopped
        esp_err_t e = esp_lcd_panel_io_tx_color(s_io, i == 0 ? 0x2C : 0x3C, p + i * chunk, chunk);
        if (e != ESP_OK) return e;
    }
    for (int i = 0; i < NUM_CHUNKS; i++) xSemaphoreTake(s_done, portMAX_DELAY);
    return ESP_OK;
}
