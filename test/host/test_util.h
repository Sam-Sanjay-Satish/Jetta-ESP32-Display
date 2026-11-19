// Test helpers: build frames from hex strings, record transmitted frames.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jd/can_frame.h"
#include "unity.h"

// Parses "10 00 02 21 63" into bytes; returns the count.
static inline size_t hex_bytes(const char *hex, uint8_t *out, size_t max)
{
    size_t n = 0;
    while (*hex && n < max) {
        while (*hex == ' ') hex++;
        if (!*hex) break;
        char buf[3] = { hex[0], hex[1], 0 };
        out[n++] = (uint8_t)strtoul(buf, NULL, 16);
        hex += 2;
    }
    return n;
}

static inline jd_frame_t F(uint16_t id, const char *hex)
{
    uint8_t b[8];
    size_t n = hex_bytes(hex, b, sizeof b);
    return jd_frame(id, b, (uint8_t)n);
}

// Records every frame a component tries to send.
typedef struct {
    jd_frame_t frames[64];
    int        count;
    bool       refuse; // pretend the bus refused the frame
} tx_log_t;

static inline bool tx_log_send(void *ctx, const jd_frame_t *f)
{
    tx_log_t *log = ctx;
    if (log->refuse) return false;
    if (log->count < 64) log->frames[log->count] = *f;
    log->count++;
    return true;
}

static inline void assert_frame(const jd_frame_t *expected, const jd_frame_t *actual, int line)
{
    char msg[64];
    snprintf(msg, sizeof msg, "frame mismatch (id %03X vs %03X)", expected->id, actual->id);
    UNITY_TEST_ASSERT(jd_frame_equal(expected, actual), line, msg);
}

// Asserts that log->frames[i] equals id#hex.
#define ASSERT_SENT(log, i, id, hex) do { \
        TEST_ASSERT_TRUE_MESSAGE((log)->count > (i), "frame not sent"); \
        jd_frame_t e_ = F(id, hex); \
        assert_frame(&e_, &(log)->frames[i], __LINE__); \
    } while (0)
