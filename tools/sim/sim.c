// Renders the firmware's gauge page on the Mac with the demo values, so the layout can be
// checked without the board. Usage: sim OUTDIR SECONDS... (writes OUTDIR/screen_<s>.ppm)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "demo.h"
#include "gauges.h"
#include "lvgl.h"

static lv_color_t fb[GAUGES_W * GAUGES_H];

static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *px)
{
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) fb[y * GAUGES_W + x] = *px++;
    lv_disp_flush_ready(d);
}

static void save(const char *dir, int sec)
{
    char path[512];
    snprintf(path, sizeof path, "%s/screen_%03d.ppm", dir, sec);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", GAUGES_W, GAUGES_H);
    for (int i = 0; i < GAUGES_W * GAUGES_H; i++) {
        uint16_t c = fb[i].full;
        uint8_t rgb[3] = { (uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 0x3F) << 2), (uint8_t)((c & 0x1F) << 3) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("%s\n", path);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: sim OUTDIR SECONDS...\n"); return 2; }
    lv_init();
    static lv_color_t buf[GAUGES_W * GAUGES_H];
    static lv_disp_draw_buf_t db;
    lv_disp_draw_buf_init(&db, buf, NULL, GAUGES_W * GAUGES_H);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res = GAUGES_W;
    drv.ver_res = GAUGES_H;
    drv.flush_cb = flush;
    drv.draw_buf = &db;
    lv_disp_drv_register(&drv);
    gauges_build(lv_scr_act());

    ui_snapshot_t snap;
    memset(&snap, 0, sizeof snap);
    snap.demo = true;
    snap.can_ok = true;
    int next = 2;
    for (uint32_t now = 0; next < argc; now += 100) { // same cadence as the firmware
        demo_fill(&snap.store, now);
        if (now % 200 == 0) gauges_refresh(&snap, now);
        lv_tick_inc(100);
        lv_timer_handler();
        if (now == (uint32_t)atoi(argv[next]) * 1000u) save(argv[1], atoi(argv[next++]));
    }
    return 0;
}
