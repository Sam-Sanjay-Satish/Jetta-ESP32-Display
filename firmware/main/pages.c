#include "pages.h"
#include <stdio.h>
#include <string.h>
#include "gauges.h"

#define COL_DIM   lv_color_hex(0x8A8A8A)
#define COL_FAINT lv_color_hex(0x505050)
#define COL_CRIT  lv_color_hex(0xFF3030)
#define COL_DEMO  lv_color_hex(0xFF40FF)

static lv_obj_t *s_scr[PAGE_COUNT];
static page_t    s_page;

// ---------------------------------------------------------------- diagnostics page

enum { ROW_LINK, ROW_MODE, ROW_RX, ROW_REP, ROW_TX, ROW_ERR, ROW_COUNT };
static const char *const ROW_NAMES[ROW_COUNT] = {
    "Link", "Mode", "Frames received", "ECU replies", "Frames sent", "Errors (rx / tx)",
};

#define ROW_Y0 38
#define ROW_H  27
#define VAL_X  140

static lv_obj_t *d_val[ROW_COUNT], *d_note;
static char      d_text[ROW_COUNT][32], d_note_text[48];
static uint16_t  d_link_color, d_note_color; // last colours set (setting one redraws the label)

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int x, int y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    return l;
}

static void set_text(lv_obj_t *l, char *mine, size_t n, const char *text)
{
    if (strcmp(mine, text) == 0) return;
    snprintf(mine, n, "%s", text);
    lv_label_set_text_static(l, mine);
}

static void build_diag(lv_obj_t *scr)
{
    label(scr, &lv_font_montserrat_20, lv_color_white(), 8, 6, "DIAGNOSTICS");
    for (int i = 0; i < ROW_COUNT; i++) {
        int y = ROW_Y0 + i * ROW_H;
        label(scr, &lv_font_montserrat_14, COL_DIM, 8, y + 3, ROW_NAMES[i]);
        d_val[i] = label(scr, &lv_font_montserrat_20, lv_color_white(), VAL_X, y, "--");
    }
    d_note = label(scr, &lv_font_montserrat_14, COL_DEMO, 8, ROW_Y0 + ROW_COUNT * ROW_H + 4, "");
    label(scr, &lv_font_montserrat_14, COL_FAINT, 8, GAUGES_H - 20, "Swipe right for the gauges");
}

static lv_color_t link_color(jd_link_t l)
{
    switch (l) {
    case JD_LINK_OK:     return lv_color_hex(0x30D158);
    case JD_LINK_HALTED: return COL_CRIT;
    case JD_LINK_LISTEN: return lv_color_hex(0x40A0FF);
    default:             return COL_DIM;
    }
}

static void set_color(lv_obj_t *l, uint16_t *mine, lv_color_t c)
{
    if (*mine == c.full) return;
    *mine = c.full;
    lv_obj_set_style_text_color(l, c, 0);
}

static void refresh_diag(const ui_snapshot_t *s)
{
    char buf[32];
    set_color(d_val[ROW_LINK], &d_link_color, link_color(s->link));
    set_text(d_val[ROW_LINK], d_text[ROW_LINK], sizeof d_text[0], jd_link_name(s->link));
    snprintf(buf, sizeof buf, "%s, tx %s",
             s->mode == JD_MODE_OBD ? "OBD" : s->mode == JD_MODE_TP20 ? "TP2.0" : "PASSIVE",
             s->tx_enabled ? "on" : "off");
    set_text(d_val[ROW_MODE], d_text[ROW_MODE], sizeof d_text[0], buf);
    const uint32_t counts[] = { [ROW_RX] = s->rx_frames, [ROW_REP] = s->replies, [ROW_TX] = s->tx_sent };
    for (int i = ROW_RX; i <= ROW_TX; i++) {
        snprintf(buf, sizeof buf, "%lu", (unsigned long)counts[i]);
        set_text(d_val[i], d_text[i], sizeof d_text[0], buf);
    }
    snprintf(buf, sizeof buf, "%lu / %lu", (unsigned long)s->rx_errors, (unsigned long)s->tx_errors);
    set_text(d_val[ROW_ERR], d_text[ROW_ERR], sizeof d_text[0], buf);

    char note[sizeof d_note_text];
    lv_color_t c = COL_DEMO;
    if (!s->can_ok) { snprintf(note, sizeof note, "CAN controller failed to start"); c = COL_CRIT; }
    else if (s->link == JD_LINK_HALTED) { snprintf(note, sizeof note, "Transmit halted: %s", jd_halt_name(s->halt)); c = COL_CRIT; }
    else if (s->demo) snprintf(note, sizeof note, "DEMO build: the gauges show fake values");
    else note[0] = '\0';
    set_color(d_note, &d_note_color, c);
    set_text(d_note, d_note_text, sizeof d_note_text, note);
}

// ---------------------------------------------------------------- navigation

static void on_gesture(lv_event_t *e)
{
    (void)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_LEFT && s_page == PAGE_GAUGES) pages_show(PAGE_DIAG);
    else if (dir == LV_DIR_RIGHT && s_page == PAGE_DIAG) pages_show(PAGE_GAUGES);
}

static lv_obj_t *new_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, on_gesture, LV_EVENT_GESTURE, NULL);
    return scr;
}

void pages_build(void)
{
    s_scr[PAGE_GAUGES] = new_screen();
    gauges_build(s_scr[PAGE_GAUGES]);
    s_scr[PAGE_DIAG] = new_screen();
    build_diag(s_scr[PAGE_DIAG]);
    pages_show(PAGE_GAUGES);
}

void pages_show(page_t p)
{
    s_page = p;
    lv_scr_load(s_scr[p]); // instant, no animation (rule 8)
}

void pages_refresh(const ui_snapshot_t *s, uint32_t now)
{
    gauges_refresh(s, now); // even when hidden, so the boost graph keeps its history
    refresh_diag(s);
}
