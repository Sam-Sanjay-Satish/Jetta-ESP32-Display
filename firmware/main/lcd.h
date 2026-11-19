// ILI9341V 240x320 over SPI, drawing rectangles (no full frame buffer: LVGL renders
// small strips, so only what changed is sent). Init sequence and settings are the
// ones proven on the bench in ripple_test (CLAUDE.md "Display settings that work").
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define LCD_W 240
#define LCD_H 320

typedef void (*lcd_done_cb_t)(void *ctx);

// done_cb runs (from an ISR) when each lcd_draw() transfer has finished.
esp_err_t lcd_init(int spi_hz, size_t max_transfer_bytes, lcd_done_cb_t done_cb, void *ctx);
// Sends a rectangle of RGB565 pixels (high byte first). Returns before the DMA is done.
esp_err_t lcd_draw(int x1, int y1, int x2, int y2, const void *pixels);
void lcd_backlight(bool on);
