#include "jd/sched.h"
#include <string.h>
#include "jd/common.h"

void jd_sched_init(jd_sched_t *s)
{
    memset(s, 0, sizeof *s);
    s->in_flight = -1;
}

int jd_sched_add(jd_sched_t *s, uint16_t period_ms, uint32_t now, uint32_t first_due)
{
    if (s->count >= JD_SCHED_MAX_ITEMS) return -1;
    s->items[s->count] = (jd_poll_item_t){ .period_ms = period_ms, .next_due = now + first_due };
    return s->count++;
}

int jd_sched_next(jd_sched_t *s, uint32_t now)
{
    if (s->in_flight >= 0 || s->count == 0) return -1;
    if (s->any_request && !jd_time_reached(now, s->last_request + JD_SCHED_MIN_GAP_MS)) return -1;
    if (s->quiet && !jd_time_reached(now, s->last_request + JD_SCHED_QUIET_PROBE_MS)) return -1;

    int best = -1;
    int32_t best_overdue = 0;
    for (int i = 0; i < s->count; i++) {
        int32_t overdue = (int32_t)(now - s->items[i].next_due);
        if (!s->quiet && overdue < 0) continue; // a quiet-mode probe ignores back-off
        if (best < 0 || overdue > best_overdue) { best = i; best_overdue = overdue; }
    }
    if (best < 0) return -1;
    s->in_flight = best;
    s->last_request = now;
    s->any_request = true;
    s->items[best].next_due = now + s->items[best].period_ms;
    s->total_requests++;
    return best;
}

void jd_sched_done(jd_sched_t *s, bool ok, uint32_t now)
{
    if (s->in_flight < 0) return;
    jd_poll_item_t *it = &s->items[s->in_flight];
    s->in_flight = -1;
    if (ok) {
        it->fails = 0;
        s->fail_streak = 0;
        if (s->quiet) { // ECU is back: everything polls again now, not after its back-off
            s->quiet = false;
            for (int i = 0; i < s->count; i++) {
                if (&s->items[i] == it) continue;
                s->items[i].fails = 0;
                s->items[i].next_due = now;
            }
        }
        return;
    }
    s->total_failures++;
    if (it->fails < 15) it->fails++;
    if (s->fail_streak < 255) s->fail_streak++;
    uint32_t backoff = (uint32_t)it->period_ms << it->fails;
    if (backoff > JD_SCHED_MAX_BACKOFF_MS) backoff = JD_SCHED_MAX_BACKOFF_MS;
    it->next_due = now + backoff;
    if (s->fail_streak >= JD_SCHED_QUIET_AFTER) s->quiet = true;
}
