// Turns ECU reply bytes into values. Integer maths only (no float on the hot path):
// a value is `value / 10^decimals` in `unit`.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    JD_UNIT_NONE = 0,
    JD_UNIT_C,       // degrees Celsius
    JD_UNIT_RPM,
    JD_UNIT_KMH,
    JD_UNIT_PCT,
    JD_UNIT_MG_STR,  // mg/stroke
    JD_UNIT_MBAR,
    JD_UNIT_BAR,
} jd_unit_t;

typedef struct {
    int32_t   value;
    uint8_t   decimals;
    jd_unit_t unit;
    bool      valid;     // false = no value (e.g. formula 25 00 00, VCDS shows a blank)
} jd_value_t;

// ---- KWP2000 measuring blocks (VCDS groups) ----
#define JD_KWP_FIELDS 4  // only the first 4 triplets are VCDS's fields 1-4

// One triplet [formula a b]. Unknown formulas give valid = false (never guessed).
jd_value_t jd_kwp_decode(uint8_t formula, uint8_t a, uint8_t b);

// Parses a `61 <group> [f a b]x8` reply into fields 1-4.
// Returns false if it isn't a reply to that group or is too short.
bool jd_kwp_parse_group(const uint8_t *msg, size_t len, uint8_t group, jd_value_t out[JD_KWP_FIELDS]);

// ---- KWP2000 fault codes (engine: 18 00 FF 00 -> 58 [count] ([hi][lo][status])...) ----
typedef struct {
    uint16_t code;    // P-code = J2012 of this (0x0401 -> P0401); VCDS's number = its decimal
    uint8_t  status;
} jd_dtc_t;

// Returns how many DTCs the ECU reported (up to `max` are stored), or -1 if malformed.
int  jd_kwp_parse_dtcs(const uint8_t *msg, size_t len, jd_dtc_t *out, int max);
void jd_dtc_pcode(uint16_t code, char out[6]);  // "P0401"
// Status byte, as VCDS shows it (macvagdiag, checked on all 21 faults of this car):
static inline bool    jd_dtc_mil(uint8_t s)          { return s & 0x80; }
static inline bool    jd_dtc_intermittent(uint8_t s) { return !(s & 0x40); }
static inline uint8_t jd_dtc_subtype(uint8_t s)      { return s & 0x1F; } // the "- 00x"

// ---- OBD-II mode 01 ----
// Decodes a `41 <pid> ...` reply for PIDs 05 (coolant), 0C (RPM), 0D (speed).
bool jd_obd_decode(const uint8_t *msg, size_t len, uint8_t pid, jd_value_t *out);

// PID 78 (EGT bank 1): `41 78 <supported mask> [hi lo]x4`; sensor 1..4.
// Returns false if the reply is malformed or the ECU marks that sensor as absent.
bool jd_obd_egt_sensor(const uint8_t *msg, size_t len, int sensor, jd_value_t *out);
