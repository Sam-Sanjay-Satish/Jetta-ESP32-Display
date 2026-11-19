#include "ui.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gauges.h"
#include "lcd.h"
#include "lvgl.h"

static const char *TAG = "ui";

#define SPI_HZ       (40 * 1000 * 1000) // 80 MHz flickered on the bench: keep 40
#define BUF_LINES    30                  // 2 x 320x30 strips = 38 KB, instead of a 150 KB frame
#define UI_PERIOD_MS 200                 // 5 Hz: labels change only if their text did; boost graph gets 1 point

static SemaphoreHandle_t s_lock;
static ui_snapshot_t     s_snap;
static bool              s_have_snap;
static lv_disp_drv_t     s_drv;

// ---------------------------------------------------------------- LVGL glue

static void flush_done(void *ctx) { lv_disp_flush_ready((lv_disp_drv_t *)ctx); }

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px)
{
    (void)drv;
    if (lcd_draw(a->x1, a->y1, a->x2, a->y2, px) != ESP_OK) lv_disp_flush_ready(&s_drv);
}

// ---------------------------------------------------------------- task

static void ui_task(void *arg)
{
    (void)arg;
    lv_init();
    size_t px = LCD_W * BUF_LINES;
    lv_color_t *b1 = heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_color_t *b2 = heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!b1 || !b2) { ESP_LOGE(TAG, "no RAM for draw buffers"); vTaskDelete(NULL); return; }
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, b1, b2, (uint32_t)px);
    lv_disp_drv_init(&s_drv);
    s_drv.hor_res = LCD_W;
    s_drv.ver_res = LCD_H;
    s_drv.flush_cb = flush_cb;
    s_drv.draw_buf = &draw_buf;
    ESP_ERROR_CHECK(lcd_init(SPI_HZ, px * sizeof(lv_color_t), flush_done, &s_drv));
    lv_disp_drv_register(&s_drv);
    gauges_build(lv_scr_act());
    lv_timer_handler();     // first full frame
    lcd_backlight(true);

    int64_t last_us = esp_timer_get_time();
    uint32_t next_refresh = 0;
    ui_snapshot_t snap;
    for (;;) {
        int64_t us = esp_timer_get_time();
        lv_tick_inc((uint32_t)((us - last_us) / 1000));
        last_us += ((us - last_us) / 1000) * 1000;
        uint32_t now = (uint32_t)(us / 1000);
        if ((int32_t)(now - next_refresh) >= 0) {
            next_refresh = now + UI_PERIOD_MS;
            bool have = false;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            if (s_have_snap) { snap = s_snap; have = true; }
            xSemaphoreGive(s_lock);
            if (have) gauges_refresh(&snap, now);
        }
        uint32_t wait = lv_timer_handler();
        if (wait > 50) wait = 50;
        if (wait < 10) wait = 10;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

void ui_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(ui_task, "ui", 6144, NULL, 3, NULL, 1);
}

void ui_publish(const ui_snapshot_t *s)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_snap = *s;
    s_have_snap = true;
    xSemaphoreGive(s_lock);
}
