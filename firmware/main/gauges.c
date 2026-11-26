#include "gauges.h"
#include <stdio.h>
#include <string.h>
#include "jd/view.h"

// ---------------------------------------------------------------- layout and ranges

// Warnings (CAN ERROR, TX HALTED) appear as a red banner over the top edge, only when needed.
#define WARN_H     20

// Two outlined boxes: on top temperatures | boost (one box, split by a vertical line),
// EGT along the bottom.
#define MARGIN     4    // screen edge to box
#define GAP        8    // between boxes
#define TOP_BOX_Y  MARGIN
#define TOP_BOX_H  (EGT_Y - GAP - TOP_BOX_Y)

// Temperatures box: oil and coolant as vertical bars, titles underneath.
#define TEMP_X     MARGIN
#define TEMP_W     150
#define BAR_W      54
#define BAR_Y      (TOP_BOX_Y + 8)
#define BAR_TITLE_Y (TOP_BOX_Y + TOP_BOX_H - 6 - 16)
#define BAR_H      (BAR_TITLE_Y - 4 - BAR_Y)
#define TITLE_W    76   // wider than the bar so "COOL (°C)" never wraps

// Bar ranges in degrees C. The bottom is "cold engine", the top is past the critical level.
#define OIL_MIN     40
#define OIL_MAX     150
#define COOL_MIN    40
#define COOL_MAX    130
#define COLD_BELOW_C 60  // display only: bar is blue until the engine is warm

// Boost area (right of the line): title, numbers and graph.
#define BOX_X       (TEMP_X + TEMP_W + GAP)
#define BOX_Y       TOP_BOX_Y
#define BOX_W       (GAUGES_W - MARGIN - BOX_X)
#define BOX_H       TOP_BOX_H
#define BOX_PAD     5
#define GRAPH_Y     64   // inside the box
#define GRAPH_W     (BOX_W - 2 * BOX_PAD)
#define GRAPH_H     (BOX_H - GRAPH_Y - BOX_PAD)
#define GRAPH_POINTS 80   // one per UI refresh (200 ms) = the last 16 s

// EGT before turbo: title, number with its unit, horizontal bar with a line at 800.
#define EGT_X       MARGIN
#define EGT_Y       192
#define EGT_W       (GAUGES_W - 2 * MARGIN)
#define EGT_H       (GAUGES_H - MARGIN - EGT_Y)
#define EGT_VAL_X   40                       // inside the box: number right-aligned in EGT_VAL_W
#define EGT_VAL_W   56
#define EGT_BAR_X   128
#define EGT_BAR_W   (EGT_W - EGT_BAR_X - 10)
#define EGT_BAR_H   16
#define EGT_MIN     100
#define EGT_MAX     900
#define EGT_LINE_C  800  // Sam 2026-11-25
// Boost is gauge pressure in mbar (0 = no boost; jd_view_number subtracts the atmosphere).
#define BOOST_MIN   -100
#define BOOST_MAX   1700

#define COL_DIM     lv_color_hex(0x8A8A8A)
#define COL_FAINT   lv_color_hex(0x505050)
#define COL_TRACK   lv_color_hex(0x1C1C1C)
#define COL_COLD    lv_color_hex(0x3A8DFF)
#define COL_OK      lv_color_hex(0x30C060)
#define COL_WARN    lv_color_hex(0xFFB000)
#define COL_CRIT    lv_color_hex(0xFF3030)
#define COL_ACT     lv_color_hex(0x00D0FF)
#define COL_SPEC    lv_color_hex(0xB0B0B0)
#define COL_BOX     lv_color_hex(0xC8C8C8)  // box outlines: slightly grey white (Sam)

// ---------------------------------------------------------------- widgets

typedef struct {
    jd_signal_t sig;
    int         min, max;
    lv_obj_t   *bar, *value;
    char        text[12];
    lv_color_t  bar_color, text_color;
    int         bar_value;
    bool        drawn;
} bar_gauge_t;

typedef struct {
    jd_signal_t sig;
    lv_obj_t   *value;
    char        text[12];
} num_t;

static bar_gauge_t g_oil = { .sig = JD_SIG_OIL, .min = OIL_MIN, .max = OIL_MAX };
static bar_gauge_t g_cool = { .sig = JD_SIG_COOLANT, .min = COOL_MIN, .max = COOL_MAX };
static bar_gauge_t g_egt = { .sig = JD_SIG_EGT, .min = EGT_MIN, .max = EGT_MAX };
static num_t       n_act = { .sig = JD_SIG_BOOST_ACT }, n_spec = { .sig = JD_SIG_BOOST_SPEC };
static lv_obj_t   *s_chart;
static lv_chart_series_t *s_ser_spec, *s_ser_act;
static lv_obj_t   *l_status;  // warning banner: hidden when all is well
static char        status_text[32];
static jd_view_t   s_view;

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int x, int y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    return l;
}

// A label of fixed width with centred text, so it never needs re-aligning.
static lv_obj_t *centred_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int x, int y, int w,
                               const char *text)
{
    lv_obj_t *l = label(parent, font, color, x, y, text);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

static int bar_y_of(const bar_gauge_t *g, int c) // screen y of temperature c on the bar
{
    return BAR_Y + BAR_H - (c - g->min) * BAR_H / (g->max - g->min);
}

static void build_bar(lv_obj_t *scr, bar_gauge_t *g, int centre_x, const char *title)
{
    int bx = centre_x - BAR_W / 2;
    centred_label(scr, &lv_font_montserrat_14, lv_color_white(), centre_x - TITLE_W / 2, BAR_TITLE_Y, TITLE_W, title);

    g->bar = lv_bar_create(scr);
    lv_obj_set_pos(g->bar, bx, BAR_Y);
    lv_obj_set_size(g->bar, BAR_W, BAR_H);
    lv_bar_set_range(g->bar, g->min, g->max);
    lv_bar_set_value(g->bar, g->min, LV_ANIM_OFF);
    lv_obj_set_style_radius(g->bar, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(g->bar, 6, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g->bar, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g->bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g->bar, COL_FAINT, LV_PART_INDICATOR);

    // Warning level: a thin amber mark across the bar.
    const jd_thresholds_t *t = jd_view_thresholds(g->sig);
    if (t->warn > g->min && t->warn < g->max) {
        lv_obj_t *m = lv_obj_create(scr);
        lv_obj_remove_style_all(m);
        lv_obj_set_pos(m, bx - 4, bar_y_of(g, t->warn) - 1);
        lv_obj_set_size(m, BAR_W + 8, 2);
        lv_obj_set_style_bg_color(m, COL_WARN, 0);
        lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
    }

    // The value sits in the middle of the bar on a black plate, so it reads on any fill.
    // Sized to its text (not the bar), so 3 digits never wrap in the narrower bar.
    g->value = label(g->bar, &lv_font_montserrat_28, COL_DIM, 0, 0, "--");
    lv_obj_align(g->value, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_hor(g->value, 2, 0);
    lv_obj_set_style_bg_color(g->value, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g->value, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g->value, 4, 0);
    lv_obj_set_style_pad_ver(g->value, 3, 0);
}

static lv_obj_t *outlined_box(lv_obj_t *scr, int x, int y, int w, int h)
{
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_remove_style_all(box);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_border_color(box, COL_BOX, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 4, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); // swipes go to the screen
    return box;
}

static void build_egt(lv_obj_t *scr)
{
    lv_obj_t *box = outlined_box(scr, EGT_X, EGT_Y, EGT_W, EGT_H);
    lv_obj_t *t = label(box, &lv_font_montserrat_14, lv_color_white(), 7, 0, "EGT");
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 7, 0);
    // Number right-aligned in a fixed width, so the unit after it never moves.
    g_egt.value = label(box, &lv_font_montserrat_24, COL_DIM, 0, 0, "--");
    lv_obj_set_width(g_egt.value, EGT_VAL_W);
    lv_obj_set_style_text_align(g_egt.value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(g_egt.value, LV_ALIGN_LEFT_MID, EGT_VAL_X, 0);
    lv_obj_t *u = label(box, &lv_font_montserrat_14, lv_color_white(), 0, 0, "\xC2\xB0" "C");
    lv_obj_align(u, LV_ALIGN_LEFT_MID, EGT_VAL_X + EGT_VAL_W + 3, 3);

    int by = (EGT_H - EGT_BAR_H) / 2;
    g_egt.bar = lv_bar_create(box);
    lv_obj_set_pos(g_egt.bar, EGT_BAR_X, by);
    lv_obj_set_size(g_egt.bar, EGT_BAR_W, EGT_BAR_H); // wider than tall: fills left to right
    lv_bar_set_range(g_egt.bar, EGT_MIN, EGT_MAX);
    lv_bar_set_value(g_egt.bar, EGT_MIN, LV_ANIM_OFF);
    lv_obj_set_style_radius(g_egt.bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(g_egt.bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_egt.bar, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_egt.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_egt.bar, COL_FAINT, LV_PART_INDICATOR);

    // The 800 C line: a short amber mark across the bar, like the warning marks on the other bars.
    lv_obj_t *m = lv_obj_create(box);
    lv_obj_remove_style_all(m);
    lv_obj_set_pos(m, EGT_BAR_X + (EGT_LINE_C - EGT_MIN) * EGT_BAR_W / (EGT_MAX - EGT_MIN) - 1, by - 4);
    lv_obj_set_size(m, 2, EGT_BAR_H + 8);
    lv_obj_set_style_bg_color(m, COL_WARN, 0);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
}

static void build_boost(lv_obj_t *scr)
{
    lv_obj_t *box = outlined_box(scr, BOX_X, BOX_Y, BOX_W, BOX_H);
    lv_obj_set_style_border_width(box, 0, 0); // just a container: the top box draws the outline

    const int half = BOX_PAD + GRAPH_W / 2 + 2;
    label(box, &lv_font_montserrat_14, lv_color_white(), BOX_PAD + 1, 3, "BOOST (mbar)");
    label(box, &lv_font_montserrat_14, COL_ACT, BOX_PAD + 1, 20, "ACTUAL");
    label(box, &lv_font_montserrat_14, COL_SPEC, half, 20, "SPEC");
    n_act.value = label(box, &lv_font_montserrat_24, COL_ACT, BOX_PAD + 1, 35, "--");
    n_spec.value = label(box, &lv_font_montserrat_24, COL_SPEC, half, 35, "--");

    s_chart = lv_chart_create(box);
    lv_obj_set_pos(s_chart, BOX_PAD, GRAPH_Y);
    lv_obj_set_size(s_chart, GRAPH_W, GRAPH_H);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, GRAPH_POINTS);
    lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, BOOST_MIN, BOOST_MAX);
    lv_chart_set_div_line_count(s_chart, 3, 0);
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(0x0C0C0C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_chart, 0, LV_PART_MAIN); // the box is the outline
    lv_obj_clear_flag(s_chart, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); // swipes go to the screen
    lv_obj_set_style_radius(s_chart, 4, LV_PART_MAIN);
    lv_obj_set_style_line_color(s_chart, lv_color_hex(0x262626), LV_PART_MAIN); // grid
    lv_obj_set_style_pad_all(s_chart, 3, LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(s_chart, 0, LV_PART_INDICATOR); // no dots on the points
    s_ser_spec = lv_chart_add_series(s_chart, COL_SPEC, LV_CHART_AXIS_PRIMARY_Y);
    s_ser_act = lv_chart_add_series(s_chart, COL_ACT, LV_CHART_AXIS_PRIMARY_Y); // drawn on top
    lv_chart_set_all_value(s_chart, s_ser_spec, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(s_chart, s_ser_act, LV_CHART_POINT_NONE);
}

void gauges_build(lv_obj_t *scr)
{
    jd_view_init(&s_view);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    outlined_box(scr, MARGIN, TOP_BOX_Y, GAUGES_W - 2 * MARGIN, TOP_BOX_H); // first, so the bars draw over it
    lv_obj_t *split = lv_obj_create(scr); // the line between the bars and the boost graph
    lv_obj_remove_style_all(split);
    lv_obj_set_pos(split, TEMP_X + TEMP_W + GAP / 2 - 1, TOP_BOX_Y);
    lv_obj_set_size(split, 1, TOP_BOX_H);
    lv_obj_set_style_bg_color(split, COL_BOX, 0);
    lv_obj_set_style_bg_opa(split, LV_OPA_COVER, 0);
    build_bar(scr, &g_oil, TEMP_X + 36, "OIL (\xC2\xB0" "C)");
    build_bar(scr, &g_cool, TEMP_X + TEMP_W - 38, "COOL (\xC2\xB0" "C)");
    build_boost(scr);
    build_egt(scr);

    // Created last so it sits on top of everything.
    l_status = label(scr, &lv_font_montserrat_14, lv_color_white(), 0, 0, "");
    lv_obj_set_size(l_status, GAUGES_W, WARN_H);
    lv_obj_set_style_text_align(l_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(l_status, 2, 0);
    lv_obj_set_style_bg_color(l_status, COL_CRIT, 0);
    lv_obj_set_style_bg_opa(l_status, LV_OPA_COVER, 0);
    lv_obj_add_flag(l_status, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------- refresh

static lv_color_t level_text_color(jd_level_t l)
{
    switch (l) {
    case JD_LEVEL_OK:   return lv_color_white();
    case JD_LEVEL_WARN: return COL_WARN;
    case JD_LEVEL_CRIT: return COL_CRIT;
    default:            return COL_DIM;
    }
}

static void set_text(lv_obj_t *l, char *mine, size_t n, const char *text)
{
    if (strcmp(mine, text) == 0) return;
    snprintf(mine, n, "%s", text);
    lv_label_set_text_static(l, mine);
}

static void refresh_bar(bar_gauge_t *g, const jd_store_t *st, uint32_t now)
{
    jd_gauge_view_t v;
    jd_view_gauge(&s_view, st, g->sig, now, &v);
    int32_t n;
    bool have = jd_view_number(st, g->sig, now, &n);
    int c = have ? (int)n : g->min;
    int fill = c < g->min ? g->min : c > g->max ? g->max : c;

    lv_color_t bar = !have                 ? COL_FAINT
                   : v.level == JD_LEVEL_CRIT ? COL_CRIT
                   : v.level == JD_LEVEL_WARN ? COL_WARN
                   : c < COLD_BELOW_C         ? COL_COLD
                                              : COL_OK;
    lv_color_t text = level_text_color(v.level);

    if (!g->drawn || fill != g->bar_value) lv_bar_set_value(g->bar, fill, LV_ANIM_OFF);
    if (!g->drawn || bar.full != g->bar_color.full) lv_obj_set_style_bg_color(g->bar, bar, LV_PART_INDICATOR);
    if (!g->drawn || text.full != g->text_color.full) lv_obj_set_style_text_color(g->value, text, 0);
    set_text(g->value, g->text, sizeof g->text, v.text);
    g->bar_value = fill;
    g->bar_color = bar;
    g->text_color = text;
    g->drawn = true;
}

static lv_coord_t refresh_num(num_t *n, const jd_store_t *st, uint32_t now)
{
    jd_gauge_view_t v;
    jd_view_gauge(&s_view, st, n->sig, now, &v);
    set_text(n->value, n->text, sizeof n->text, v.text);
    int32_t x;
    return jd_view_number(st, n->sig, now, &x) ? (lv_coord_t)x : LV_CHART_POINT_NONE;
}

void gauges_refresh(const ui_snapshot_t *s, uint32_t now)
{
    refresh_bar(&g_oil, &s->store, now);
    refresh_bar(&g_cool, &s->store, now);
    refresh_bar(&g_egt, &s->store, now);
    lv_coord_t act = refresh_num(&n_act, &s->store, now);
    lv_coord_t spec = refresh_num(&n_spec, &s->store, now);
    lv_chart_set_next_value(s_chart, s_ser_spec, spec);
    lv_chart_set_next_value(s_chart, s_ser_act, act);

    // Normal link state and the counters live on the diagnostics page (pages.c).
    char buf[sizeof status_text];
    if (!s->can_ok) snprintf(buf, sizeof buf, "CAN ERROR");
    else if (s->link == JD_LINK_HALTED) snprintf(buf, sizeof buf, "TX HALTED: %s", jd_halt_name(s->halt));
    else buf[0] = '\0';
    bool was = status_text[0] != '\0';
    set_text(l_status, status_text, sizeof status_text, buf);
    if (was != (buf[0] != '\0')) {
        if (buf[0]) lv_obj_clear_flag(l_status, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(l_status, LV_OBJ_FLAG_HIDDEN);
    }
}
