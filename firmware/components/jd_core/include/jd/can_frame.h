// A classic CAN frame with an 11-bit ID. Plain data, no hardware dependency.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint16_t id;      // 11-bit standard ID
    uint8_t  len;     // 0..8
    uint8_t  data[8];
} jd_frame_t;

// Builds a frame from bytes. len > 8 is clamped to 8.
static inline jd_frame_t jd_frame(uint16_t id, const uint8_t *data, uint8_t len)
{
    jd_frame_t f = { .id = id, .len = (uint8_t)(len > 8 ? 8 : len) };
    if (data && f.len) memcpy(f.data, data, f.len);
    return f;
}

static inline bool jd_frame_equal(const jd_frame_t *a, const jd_frame_t *b)
{
    return a->id == b->id && a->len == b->len && memcmp(a->data, b->data, a->len) == 0;
}

// Where frames go out. The firmware's implementation is the ONLY path to the
// TWAI transmit call, and it runs jd_guard_check() first. Returns true if sent.
typedef bool (*jd_tx_fn)(void *ctx, const jd_frame_t *frame);
