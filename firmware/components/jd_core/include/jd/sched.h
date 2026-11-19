// Gentle poll scheduler (HARD RULE 4). One request in flight at a time, a minimum gap
// between requests, per-item back-off on failure, and a quiet mode when the ECU stops
// answering (ignition off / engine asleep): then only a rare single probe.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define JD_SCHED_MAX_ITEMS      8
#define JD_SCHED_MIN_GAP_MS     300    // never more than ~3 requests/s in total
#define JD_SCHED_MAX_BACKOFF_MS 30000
#define JD_SCHED_QUIET_AFTER    5      // consecutive failures (any item) before quiet mode
#define JD_SCHED_QUIET_PROBE_MS 5000   // one probe this often while quiet (0.2 req/s)

typedef struct {
    uint16_t period_ms;
    uint32_t next_due;
    uint8_t  fails;      // consecutive failures of this item
} jd_poll_item_t;

typedef struct {
    jd_poll_item_t items[JD_SCHED_MAX_ITEMS];
    int      count;
    int      in_flight;          // item index, or -1
    uint32_t last_request;
    bool     any_request;
    uint8_t  fail_streak;        // consecutive failures over all items
    bool     quiet;
    uint32_t total_requests, total_failures;
} jd_sched_t;

void jd_sched_init(jd_sched_t *s);
// Adds an item; returns its index (or -1 if full). First poll is due `first_due` ms from `now`.
int  jd_sched_add(jd_sched_t *s, uint16_t period_ms, uint32_t now, uint32_t first_due);
// The item to request now, or -1. Marks it in flight.
int  jd_sched_next(jd_sched_t *s, uint32_t now);
// Report the outcome of the in-flight request.
void jd_sched_done(jd_sched_t *s, bool ok, uint32_t now);
