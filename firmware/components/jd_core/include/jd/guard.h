// Read-only allowlist (HARD RULES 1-3). Every frame is checked here before the
// TWAI transmit call; anything not on the list is refused.
//
// An entry matches on CAN ID, exact length and exact bytes. The only bytes that
// may vary are the TP 2.0 sequence counters (low nibble of byte 0), marked by
// the entry's mask. Nothing else is a wildcard.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "jd/can_frame.h"

typedef struct {
    const char *name;   // for logs
    uint16_t    id;
    uint8_t     len;
    uint8_t     data[8];
    uint8_t     mask[8]; // 0xFF = byte must match exactly, 0xF0 = low nibble is a sequence number
} jd_allow_entry_t;

// The fixed allowlist, and its size.
extern const jd_allow_entry_t jd_allowlist[];
extern const size_t           jd_allowlist_len;

// Returns the matching entry, or NULL if the frame is forbidden.
const jd_allow_entry_t *jd_guard_match(const jd_frame_t *frame);

static inline bool jd_guard_check(const jd_frame_t *frame) { return jd_guard_match(frame) != NULL; }
