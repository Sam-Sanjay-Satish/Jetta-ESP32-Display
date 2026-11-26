// The screens and how to move between them. Swipe left on the gauges for the diagnostics
// page, swipe right to go back. Nothing needed while driving is behind a swipe (rule 8):
// the gauge page is what boots and it carries the CAN ERROR / TX HALTED warnings.
// Only LVGL + jd_core, no ESP-IDF calls, so tools/sim can render every page on the Mac.
#pragma once
#include <stdint.h>
#include "lvgl.h"
#include "ui.h"

typedef enum { PAGE_GAUGES, PAGE_DIAG, PAGE_COUNT } page_t;

void pages_build(void);                                   // creates every page, shows the gauges
void pages_refresh(const ui_snapshot_t *s, uint32_t now); // every UI period, all pages
void pages_show(page_t p);
