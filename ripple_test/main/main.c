// Phase 0: touch ripple test for the LCDWIKI ES3C28P. No CAN, no TWAI, no car code.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display_ili9341.h"
#include "touch_ft6336.h"

// ============================ TUNE THESE ============================
#define LCD_SPI_HZ       (40 * 1000 * 1000)   // try 80 * 1000 * 1000 and compare FPS
// Display. Vendor init uses invert=1, bgr=1.
//   Colours look like a negative (black<->white)      -> flip LCD_INVERT
//   Red and blue swapped (the boot bars say so)       -> flip LCD_BGR
//   Image mirrored left-right / upside-down           -> LCD_MIRROR_X / LCD_MIRROR_Y
//   Want landscape                                    -> LCD_SWAP_XY 1 (screen becomes 320x240)
#define LCD_INVERT       1
#define LCD_BGR          1
#define LCD_SWAP_XY      0
#define LCD_MIRROR_X     0
#define LCD_MIRROR_Y     0
// Touch. Check the ripple appears under your finger:
//   Ripple on the opposite side left<->right          -> TOUCH_MIRROR_X
//   Ripple on the opposite side top<->bottom          -> TOUCH_MIRROR_Y
//   Moving finger left-right moves ripple up-down     -> TOUCH_SWAP_XY
#define TOUCH_SWAP_XY    0
#define TOUCH_MIRROR_X   0
#define TOUCH_MIRROR_Y   0
// ====================================================================

#define MAX_RIPPLES      16
#define RIPPLE_SPEED     220.0f   // px per second
#define RIPPLE_LIFE_S    1.2f
#define HOLD_SPAWN_US    90000
#define HUE_STEP         137.0f

static const char *TAG = "ripple";

// ---------------- tiny 5x7 font (columns, bit0 = top row) ----------------
typedef struct { char c; uint8_t col[5]; } glyph_t;
static const glyph_t FONT[] = {
    {'0', {0x3E,0x51,0x49,0x45,0x3E}}, {'1', {0x00,0x42,0x7F,0x40,0x00}},
    {'2', {0x42,0x61,0x51,0x49,0x46}}, {'3', {0x21,0x41,0x45,0x4B,0x31}},
    {'4', {0x18,0x14,0x12,0x7F,0x10}}, {'5', {0x27,0x45,0x45,0x45,0x39}},
    {'6', {0x3C,0x4A,0x49,0x49,0x30}}, {'7', {0x01,0x71,0x09,0x05,0x03}},
    {'8', {0x36,0x49,0x49,0x49,0x36}}, {'9', {0x06,0x49,0x49,0x29,0x1E}},
    {'.', {0x00,0x60,0x60,0x00,0x00}}, {':', {0x00,0x36,0x36,0x00,0x00}},
    {'C', {0x3E,0x41,0x41,0x41,0x22}}, {'D', {0x7F,0x41,0x41,0x22,0x1C}},
    {'E', {0x7F,0x49,0x49,0x49,0x41}}, {'F', {0x7F,0x09,0x09,0x09,0x01}},
    {'H', {0x7F,0x08,0x08,0x08,0x7F}}, {'I', {0x00,0x41,0x7F,0x41,0x00}},
    {'M', {0x7F,0x02,0x0C,0x02,0x7F}}, {'N', {0x7F,0x04,0x08,0x10,0x7F}},
    {'P', {0x7F,0x09,0x09,0x09,0x06}}, {'R', {0x7F,0x09,0x19,0x29,0x46}},
    {'S', {0x46,0x49,0x49,0x49,0x31}}, {'T', {0x01,0x01,0x7F,0x01,0x01}},
};

static void draw_text(uint16_t *fb, int W, int H, int x, int y, int scale, uint16_t col, const char *s)
{
    for (; *s; s++, x += 6 * scale) {
        const glyph_t *g = NULL;
        for (size_t i = 0; i < sizeof FONT / sizeof FONT[0]; i++) if (FONT[i].c == *s) { g = &FONT[i]; break; }
        if (!g) continue;
        for (int cx = 0; cx < 5; cx++)
            for (int cy = 0; cy < 7; cy++)
                if (g->col[cx] & (1 << cy))
                    for (int dy = 0; dy < scale; dy++)
                        for (int dx = 0; dx < scale; dx++) {
                            int px = x + cx * scale + dx, py = y + cy * scale + dy;
                            if (px >= 0 && px < W && py >= 0 && py < H) fb[py * W + px] = col;
                        }
    }
}

// ---------------- colour ----------------
static void hsv_to_rgb(float h, uint8_t *r, uint8_t *g, uint8_t *b)   // s = v = 1
{
    float x = 1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f);
    float rr = 0, gg = 0, bb = 0;
    int sector = (int)(h / 60.0f) % 6;
    switch (sector) {
        case 0: rr = 1; gg = x; break;  case 1: rr = x; gg = 1; break;
        case 2: gg = 1; bb = x; break;  case 3: gg = x; bb = 1; break;
        case 4: rr = x; bb = 1; break;  default: rr = 1; bb = x; break;
    }
    *r = rr * 255; *g = gg * 255; *b = bb * 255;
}

// ---------------- ripples ----------------
typedef struct { bool active; float cx, cy; int64_t t0; uint8_t r, g, b; } ripple_t;
static ripple_t ripples[MAX_RIPPLES];

static void spawn(int x, int y, uint8_t r, uint8_t g, uint8_t b, int64_t now)
{
    int slot = 0;
    for (int i = 0; i < MAX_RIPPLES; i++) {
        if (!ripples[i].active) { slot = i; break; }
        if (ripples[i].t0 < ripples[slot].t0) slot = i;   // none free: replace the oldest
    }
    ripples[slot] = (ripple_t){ true, x, y, now, r, g, b };
}

static inline void hspan(uint16_t *fb, int W, int H, int y, int x0, int x1, uint16_t col)
{
    if (y < 0 || y >= H) return;
    if (x0 < 0) x0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    uint16_t *p = fb + y * W;
    for (int x = x0; x <= x1; x++) p[x] = col;
}

// Ring between radius ri and ro, drawn row by row as 1 or 2 spans.
static void draw_ring(uint16_t *fb, int W, int H, int cx, int cy, int ro, int ri, uint16_t col)
{
    if (ro <= 0) return;
    if (ri < 0) ri = 0;
    int y0 = cy - ro < 0 ? 0 : cy - ro, y1 = cy + ro > H - 1 ? H - 1 : cy + ro;
    for (int y = y0; y <= y1; y++) {
        int dy = y - cy;
        int xo = (int)sqrtf((float)(ro * ro - dy * dy));
        if (abs(dy) < ri) {
            int xi = (int)sqrtf((float)(ri * ri - dy * dy));
            hspan(fb, W, H, y, cx - xo, cx - xi, col);
            hspan(fb, W, H, y, cx + xi, cx + xo, col);
        } else {
            hspan(fb, W, H, y, cx - xo, cx + xo, col);
        }
    }
}

static void draw_ripples(uint16_t *fb, int W, int H, int64_t now)
{
    for (int i = 0; i < MAX_RIPPLES; i++) {
        ripple_t *r = &ripples[i];
        if (!r->active) continue;
        float age = (now - r->t0) / 1e6f;
        if (age >= RIPPLE_LIFE_S) { r->active = false; continue; }
        float fade = 1.0f - age / RIPPLE_LIFE_S;
        int R = (int)(age * RIPPLE_SPEED);
        int b1 = (int)(256 * fade), b2 = (int)(256 * fade * 0.6f);
        draw_ring(fb, W, H, (int)r->cx, (int)r->cy, R, R - 4, DISPLAY_RGB565((r->r * b1) >> 8, (r->g * b1) >> 8, (r->b * b1) >> 8));
        if (R > 22)
            draw_ring(fb, W, H, (int)r->cx, (int)r->cy, R - 20, R - 23, DISPLAY_RGB565((r->r * b2) >> 8, (r->g * b2) >> 8, (r->b * b2) >> 8));
    }
}

// Boot-time colour bars: red, green, blue, white (top to bottom). Wrong colours here = wrong LCD_BGR / LCD_INVERT.
static void show_color_bars(uint16_t *fb, int W, int H)
{
    const uint16_t cols[4] = { DISPLAY_RGB565(255,0,0), DISPLAY_RGB565(0,255,0), DISPLAY_RGB565(0,0,255), DISPLAY_RGB565(255,255,255) };
    for (int y = 0; y < H; y++) hspan(fb, W, H, y, 0, W - 1, cols[y * 4 / H]);
    display_push();
    vTaskDelay(pdMS_TO_TICKS(2500));
}

typedef struct { bool down; int64_t last_spawn; uint8_t r, g, b; } slot_t;

void app_main(void)
{
    display_config_t dc = { .spi_hz = LCD_SPI_HZ, .invert = LCD_INVERT, .bgr = LCD_BGR,
                            .swap_xy = LCD_SWAP_XY, .mirror_x = LCD_MIRROR_X, .mirror_y = LCD_MIRROR_Y };
    ESP_ERROR_CHECK(display_init(&dc));
    const int W = display_width(), H = display_height();
    uint16_t *fb = display_framebuffer();

    touch_config_t tc = { .width = W, .height = H, .swap_xy = TOUCH_SWAP_XY,
                          .mirror_x = TOUCH_MIRROR_X, .mirror_y = TOUCH_MIRROR_Y };
    esp_err_t te = touch_init(&tc);
    if (te != ESP_OK) ESP_LOGE(TAG, "touch init failed: %s (display test continues)", esp_err_to_name(te));

    show_color_bars(fb, W, H);

    slot_t slots[2] = {0};
    float hue = 0;
    // HUD values = averages over the last full second
    float h_fps = 0, h_render = 0, h_spi = 0, h_touch = 0;
    int frames = 0, i2c_err = 0, touches = 0;
    int64_t sum_render = 0, sum_spi = 0, sum_touch = 0, win_start = esp_timer_get_time();

    while (1) {
        // --- touch ---
        int64_t t0 = esp_timer_get_time();
        touch_point_t pts[2];
        int n = touch_read(pts);
        int64_t t1 = esp_timer_get_time();
        sum_touch += t1 - t0;
        if (n < 0) { i2c_err++; n = 0; }

        bool seen[2] = { false, false };
        for (int i = 0; i < n; i++) {
            int s = pts[i].id < 2 ? pts[i].id : i;
            seen[s] = true;
            if (!slots[s].down) {   // new touch: new colour
                hue = fmodf(hue + HUE_STEP, 360.0f);
                hsv_to_rgb(hue, &slots[s].r, &slots[s].g, &slots[s].b);
                slots[s].down = true;
                slots[s].last_spawn = t1;
                touches++;
                spawn(pts[i].x, pts[i].y, slots[s].r, slots[s].g, slots[s].b, t1);
                ESP_LOGI(TAG, "touch id=%d raw=(%d,%d) screen=(%d,%d)", pts[i].id, pts[i].raw_x, pts[i].raw_y, pts[i].x, pts[i].y);
            } else if (t1 - slots[s].last_spawn >= HOLD_SPAWN_US) {   // held/dragged
                slots[s].last_spawn = t1;
                spawn(pts[i].x, pts[i].y, slots[s].r, slots[s].g, slots[s].b, t1);
            }
        }
        for (int s = 0; s < 2; s++) if (!seen[s]) slots[s].down = false;

        // --- render ---
        int64_t r0 = esp_timer_get_time();
        memset(fb, 0, (size_t)W * H * 2);
        draw_ripples(fb, W, H, r0);
        char line[24];
        snprintf(line, sizeof line, "FPS %4.1f", h_fps);       draw_text(fb, W, H, 4, 4,  2, 0xFFFF, line);
        snprintf(line, sizeof line, "RND %4.1fMS", h_render);  draw_text(fb, W, H, 4, 20, 2, 0xFFFF, line);
        snprintf(line, sizeof line, "SPI %4.1fMS", h_spi);     draw_text(fb, W, H, 4, 36, 2, 0xFFFF, line);
        snprintf(line, sizeof line, "TCH %4.1fMS", h_touch);   draw_text(fb, W, H, 4, 52, 2, 0xFFFF, line);
        int64_t r1 = esp_timer_get_time();

        // --- push (DMA, waits until done) ---
        display_push();
        int64_t r2 = esp_timer_get_time();
        sum_render += r1 - r0;
        sum_spi += r2 - r1;
        frames++;

        if (r2 - win_start >= 1000000) {
            float secs = (r2 - win_start) / 1e6f;
            h_fps = frames / secs;
            h_render = sum_render / 1000.0f / frames;
            h_spi = sum_spi / 1000.0f / frames;
            h_touch = sum_touch / 1000.0f / frames;
            ESP_LOGI(TAG, "fps=%.1f render=%.2fms spi=%.2fms touch=%.2fms touches=%d i2c_err=%d",
                     h_fps, h_render, h_spi, h_touch, touches, i2c_err);
            frames = 0; sum_render = sum_spi = sum_touch = 0; win_start = r2;
        }
    }
}
