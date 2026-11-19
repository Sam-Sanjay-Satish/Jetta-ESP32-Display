// Fake values for trying the screen without a car (menuconfig "Demo mode").
// Plain C, no ESP-IDF: tools/sim uses it too. The screen shows "DEMO - FAKE VALUES"
// whenever these are on screen.
//
// A 3-minute loop: cold start, warm-up, normal driving, then a hot spell (oil goes
// critical, coolant into warning) so every colour shows, then back to a cold start.
// Boost: a throttle pulse every 7 s, actual lagging specified; every 4th pulse underboosts.
#pragma once
#include <stdint.h>
#include "jd/store.h"

#define DEMO_LOOP_MS 180000

void demo_fill(jd_store_t *s, uint32_t now_ms); // call often (every 100 ms or so)
