// ILI9341V 240x320 over SPI, full-frame RGB565 buffer pushed by DMA.
// Reusable: no dependency on the rest of the test.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    int  spi_hz;
    bool invert;     // panel colour inversion (vendor init turns this ON)
    bool bgr;        // BGR colour order (vendor init uses BGR)
    bool swap_xy;    // landscape (320x240)
    bool mirror_x;
    bool mirror_y;
} display_config_t;

esp_err_t display_init(const display_config_t *cfg);
int       display_width(void);
int       display_height(void);
// Frame buffer in internal DMA RAM. Pixels are stored BYTE-SWAPPED (the SPI
// sends the high byte first), use DISPLAY_RGB565() to build them.
uint16_t *display_framebuffer(void);
// Sends the whole frame buffer. Blocks until the DMA transfer is finished.
esp_err_t display_push(void);

#define DISPLAY_RGB565(r, g, b) \
    __builtin_bswap16((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
