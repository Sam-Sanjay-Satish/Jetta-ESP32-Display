// Latest value of each signal, fed from any decoded ECU reply (ours or overheard).
#pragma once
#include <stddef.h>
#include "jd/decode.h"

typedef enum {
    JD_SIG_EGT = 0,   // exhaust gas temperature before turbo
    JD_SIG_COOLANT,
    JD_SIG_OIL,       // no source yet (engine has none; cluster TBD)
    JD_SIG_RPM,
    JD_SIG_COUNT
} jd_signal_t;

#define JD_STORE_STALE_MS 3000  // older than this = no data on screen

typedef struct {
    jd_value_t value;
    uint32_t   at;      // when it was updated
    bool       seen;
} jd_reading_t;

typedef struct {
    jd_reading_t r[JD_SIG_COUNT];
    uint32_t     updates;
} jd_store_t;

void jd_store_init(jd_store_t *s);
// Recognises 61 63 (group 099), 61 07 (group 007), 41 05 / 41 0C / 41 78.
// Returns how many signals it updated (0 = not a message we use).
int  jd_store_apply(jd_store_t *s, const uint8_t *msg, size_t len, uint32_t now);
// The reading if it is fresh, else NULL.
const jd_reading_t *jd_store_get(const jd_store_t *s, jd_signal_t sig, uint32_t now);
