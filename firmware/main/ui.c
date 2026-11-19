#include "ui.h"
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "jd/view.h"
#include "lcd.h"
#include "lvgl.h"

static const char *TAG = "ui";

#define SPI_HZ       (40 * 1000 * 1000) // 80 MHz flickered on the bench: keep 40
#define BUF_LINES    40                  // 2 x 240x40 strips = 38 KB, instead of a 150 KB frame
#define UI_PERIOD_MS 200                 // label refresh: 5 Hz is plenty for temperatures

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

// ---------------------------------------------------------------- widgets

typedef struct {
    lv_obj_t  *value;
    char       text[12];
    jd_level_t level;
    bool       drawn;
} gauge_t;

static gauge_t   g_egt, g_cool, g_oil, g_rpm;
static lv_obj_t *l_status, *l_info;
static char      status_text[32], info_text[64];
static jd_view_t s_view;

static lv_color_t level_color(jd_level_t l)
{
    switch (l) {
    case JD_LEVEL_OK:   return lv_color_white();
    case JD_LEVEL_WARN: return lv_color_hex(0xFFB000);
    case JD_LEVEL_CRIT: return lv_color_hex(0xFF3030);
    default:            return lv_color_hex(0x606060);
    }
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, lv_align_t align,
                       int x, int y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    lv_obj_align(l, align, x, y);
    return l;
}

static void build_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_color_t dim = lv_color_hex(0x8A8A8A);

    l_status = label(scr, &lv_font_montserrat_14, dim, LV_ALIGN_TOP_MID, 0, 4, "");

    // EGT: the main gauge
    label(scr, &lv_font_montserrat_20, dim, LV_ALIGN_TOP_MID, 0, 34, "EGT  \xC2\xB0" "C");
    g_egt.value = label(scr, &lv_font_montserrat_48, level_color(JD_LEVEL_NONE), LV_ALIGN_TOP_MID, 0, 62, "--");

    // Coolant | Oil
    label(scr, &lv_font_montserrat_14, dim, LV_ALIGN_TOP_MID, -60, 150, "COOLANT \xC2\xB0" "C");
    label(scr, &lv_font_montserrat_14, dim, LV_ALIGN_TOP_MID, 60, 150, "OIL \xC2\xB0" "C");
    g_cool.value = label(scr, &lv_font_montserrat_28, level_color(JD_LEVEL_NONE), LV_ALIGN_TOP_MID, -60, 172, "--");
    g_oil.value = label(scr, &lv_font_montserrat_28, level_color(JD_LEVEL_NONE), LV_ALIGN_TOP_MID, 60, 172, "--");

    // RPM
    label(scr, &lv_font_montserrat_14, dim, LV_ALIGN_TOP_MID, 0, 226, "RPM");
    g_rpm.value = label(scr, &lv_font_montserrat_28, level_color(JD_LEVEL_NONE), LV_ALIGN_TOP_MID, 0, 246, "--");

    l_info = label(scr, &lv_font_montserrat_14, lv_color_hex(0x505050), LV_ALIGN_BOTTOM_MID, 0, -4, "");
}

static void set_gauge(gauge_t *g, const jd_gauge_view_t *v)
{
    if (g->drawn && g->level == v->level && strcmp(g->text, v->text) == 0) return; // nothing changed
    if (strcmp(g->text, v->text) != 0 || !g->drawn) {
        strcpy(g->text, v->text);
        lv_label_set_text_static(g->value, g->text);
    }
    if (g->level != v->level || !g->drawn) lv_obj_set_style_text_color(g->value, level_color(v->level), 0);
    g->level = v->level;
    g->drawn = true;
}

static lv_color_t link_color(jd_link_t l)
{
    switch (l) {
    case JD_LINK_OK:     return lv_color_hex(0x30D158);
    case JD_LINK_HALTED: return lv_color_hex(0xFF3030);
    case JD_LINK_LISTEN: return lv_color_hex(0x40A0FF);
    default:             return lv_color_hex(0x8A8A8A);
    }
}

static const char *mode_name(jd_mode_t m)
{
    return m == JD_MODE_OBD ? "OBD" : m == JD_MODE_TP20 ? "TP2.0" : "PASSIVE";
}

static void refresh(const ui_snapshot_t *s, uint32_t now)
{
    jd_gauge_view_t v;
    jd_view_gauge(&s_view, &s->store, JD_SIG_EGT, now, &v);     set_gauge(&g_egt, &v);
    jd_view_gauge(&s_view, &s->store, JD_SIG_COOLANT, now, &v); set_gauge(&g_cool, &v);
    jd_view_gauge(&s_view, &s->store, JD_SIG_OIL, now, &v);     set_gauge(&g_oil, &v);
    jd_view_gauge(&s_view, &s->store, JD_SIG_RPM, now, &v);     set_gauge(&g_rpm, &v);

    char buf[sizeof status_text];
    if (!s->can_ok) snprintf(buf, sizeof buf, "CAN ERROR");
    else if (s->link == JD_LINK_HALTED) snprintf(buf, sizeof buf, "TX HALTED: %s", jd_halt_name(s->halt));
    else snprintf(buf, sizeof buf, "%s  %s", jd_link_name(s->link), mode_name(s->mode));
    if (strcmp(buf, status_text) != 0) {
        strcpy(status_text, buf);
        lv_label_set_text_static(l_status, status_text);
        lv_obj_set_style_text_color(l_status, s->can_ok ? link_color(s->link) : lv_color_hex(0xFF3030), 0);
    }
    char ibuf[sizeof info_text];
    snprintf(ibuf, sizeof ibuf, "rx %lu  rep %lu  tx %lu  err %lu/%lu", (unsigned long)s->rx_frames,
             (unsigned long)s->replies, (unsigned long)s->tx_sent, (unsigned long)s->rx_errors,
             (unsigned long)s->tx_errors);
    if (strcmp(ibuf, info_text) != 0) {
        strcpy(info_text, ibuf);
        lv_label_set_text_static(l_info, info_text);
    }
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
    jd_view_init(&s_view);
    build_screen();
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
            if (have) refresh(&snap, now);
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
