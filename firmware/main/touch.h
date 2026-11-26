// FT6336G capacitive touch (I2C, polled), mapped to the landscape screen (320x240).
// Ported from ripple_test/main/touch_ft6336.c, which proved the chip and its axes on the
// replacement board (2026-11-25): in portrait (MADCTL 0x08) raw x/y are screen x/y.
#pragma once
#include <stdbool.h>
#include "esp_err.h"

// Fails (and the display carries on without touch) if the chip doesn't answer.
esp_err_t touch_init(void);
// true while a finger is down; *x, *y = first finger in landscape screen coordinates.
// Returns false on no touch, I2C error, or if touch_init failed.
bool touch_get(int *x, int *y);
