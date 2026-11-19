// What the screen shows for each gauge: text + warning level. Pure logic, so the
// UI layer only draws. Levels use hysteresis so a value near a threshold doesn't
// make the screen flicker (HARD RULE 8).
#pragma once
#include "jd/store.h"

typedef enum {
    JD_LEVEL_NONE = 0, // no data / stale
    JD_LEVEL_OK,
    JD_LEVEL_WARN,
    JD_LEVEL_CRIT,
} jd_level_t;

typedef struct {
    int16_t warn, crit;  // whole units (degrees C); crit > warn. 0/0 = no levels
    int16_t hysteresis;  // a level clears only below threshold - hysteresis
} jd_thresholds_t;

// PLACEHOLDERS: Sam to confirm sensible limits for the CJAA before relying on them.
#define JD_EGT_WARN_C      700
#define JD_EGT_CRIT_C      800
#define JD_COOLANT_WARN_C  110
#define JD_COOLANT_CRIT_C  118
#define JD_OIL_WARN_C      130
#define JD_OIL_CRIT_C      140
#define JD_LEVEL_HYST_C    10

// Boost is shown as gauge pressure (Sam, 2026-11-19): absolute - standard atmosphere.
// Fixed, not measured: near sea level the error is a few tens of mbar.
#define JD_BOOST_ATMOS_MBAR 1013

typedef struct {
    char       text[12];  // "162", "--"
    jd_level_t level;
} jd_gauge_view_t;

typedef struct {
    jd_level_t last[JD_SIG_COUNT]; // remembered for hysteresis
} jd_view_t;

const jd_thresholds_t *jd_view_thresholds(jd_signal_t sig);
// Level for `whole` units, given the level shown before.
jd_level_t jd_level_eval(jd_level_t prev, int32_t whole, const jd_thresholds_t *t);
// Rounds a value to whole units (half away from zero).
int32_t jd_value_whole(const jd_value_t *v);

// The number shown for a signal, in whole display units (boost: gauge mbar).
// False if there is no fresh reading.
bool jd_view_number(const jd_store_t *s, jd_signal_t sig, uint32_t now, int32_t *out);

void jd_view_init(jd_view_t *v);
void jd_view_gauge(jd_view_t *v, const jd_store_t *s, jd_signal_t sig, uint32_t now, jd_gauge_view_t *out);
