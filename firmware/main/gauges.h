// The gauge page (landscape 320x240): oil and coolant as vertical bars, boost
// specified vs actual as a scrolling two-line graph. Only LVGL + jd_core, no ESP-IDF
// calls, so tools/sim can render the same screen on the Mac.
#pragma once
#include <stdint.h>
#include "lvgl.h"
#include "ui.h"

#define GAUGES_W 320
#define GAUGES_H 240

void gauges_build(lv_obj_t *scr);
// Call every UI period: updates what changed and pushes one point onto the boost graph.
void gauges_refresh(const ui_snapshot_t *s, uint32_t now);
