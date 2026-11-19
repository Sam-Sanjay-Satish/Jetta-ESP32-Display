// FT6336G capacitive touch over I2C (polled). Reusable, no ripple-test code in here.
#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    int  width, height;          // screen size the coordinates are mapped to
    bool swap_xy, mirror_x, mirror_y;
} touch_config_t;

typedef struct {
    int id;                      // finger id from the chip (0 or 1)
    int x, y;                    // mapped to screen coordinates
    int raw_x, raw_y;            // as reported by the chip
} touch_point_t;

esp_err_t touch_init(const touch_config_t *cfg);
// Returns number of fingers (0..2) and fills out[], or a negative value on I2C error.
int touch_read(touch_point_t out[2]);
