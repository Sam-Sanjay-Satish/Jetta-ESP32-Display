#include "jd/diag.h"
#include <string.h>

typedef struct {
    uint8_t  bytes[JD_TP20_MAX_REQUEST];
    uint8_t  len;
    uint16_t period_ms;
} poll_def_t;

// What each mode polls. Every request here must also be on the allowlist (guard.c).
static const poll_def_t OBD_POLLS[] = {
    { { 0x01, 0x78 }, 2, 1000 },  // EGT
    { { 0x01, 0x0C }, 2, 2000 },  // RPM
    { { 0x01, 0x05 }, 2, 5000 },  // coolant (changes slowly)
};
static const poll_def_t TP20_POLLS[] = {
    { { 0x21, 0x0B }, 2, 500 },   // group 011: RPM + boost specified/actual (the graph: 2 points/s)
    { { 0x21, 0x63 }, 2, 2000 },  // group 099: EGT before turbo (Sam 2026-11-25); 2 s keeps the
                                  // total (2.7/s) under the scheduler's 300 ms gap with room for 011
    { { 0x21, 0x07 }, 2, 5000 },  // group 007: coolant (changes slowly)
};
// What we send after opening the channel, before any group read. VCDS (2026-11-18):
// 10 89, 1A 9B, 31 B8 00 00 (routine control: never), 1A 91. Only the session is required;
// identification is sent like VCDS does in case the engine expects it, and a refusal is ignored.
static const struct { uint8_t bytes[2]; bool required; } TP20_INIT[] = {
    { { 0x10, 0x89 }, true },
    { { 0x1A, 0x9B }, false },
};
#define TP20_INIT_STEPS (sizeof TP20_INIT / sizeof TP20_INIT[0])

void jd_diag_init(jd_diag_t *d, jd_mode_t mode, jd_txgate_t *gate, uint32_t now)
{
    memset(d, 0, sizeof *d);
    d->mode = mode;
    d->gate = gate;
    jd_isotp_init(&d->obd, 0x7E0, 0x7E8, jd_txgate_send, gate);
    jd_tp20_init(&d->tp, jd_txgate_send, gate);
    jd_sched_init(&d->sched);
    jd_sniff_init(&d->sniff);
    jd_store_init(&d->store);
    d->tp_reopen_at = now;

    const poll_def_t *polls = NULL;
    size_t n = 0;
    if (mode == JD_MODE_OBD)  { polls = OBD_POLLS;  n = sizeof OBD_POLLS / sizeof OBD_POLLS[0]; }
    if (mode == JD_MODE_TP20) { polls = TP20_POLLS; n = sizeof TP20_POLLS / sizeof TP20_POLLS[0]; }
    for (size_t i = 0; i < n; i++) {
        int idx = jd_sched_add(&d->sched, polls[i].period_ms, now, 0);
        if (idx < 0) break;
        memcpy(d->req[idx].bytes, polls[i].bytes, polls[i].len);
        d->req[idx].len = polls[i].len;
    }
}

static bool transmitting(const jd_diag_t *d)
{
    return d->mode != JD_MODE_PASSIVE && jd_txgate_can_send(d->gate);
}

static void got_reply(jd_diag_t *d, const uint8_t *msg, size_t len, uint32_t now)
{
    jd_store_apply(&d->store, msg, len, now);
    d->replies++;
    d->last_reply_at = now;
    d->any_reply = true;
}

void jd_diag_on_frame(jd_diag_t *d, const jd_frame_t *f, uint32_t now)
{
    if (!transmitting(d)) {
        const uint8_t *msg;
        size_t len;
        if (jd_sniff_feed(&d->sniff, f, &msg, &len)) jd_store_apply(&d->store, msg, len, now);
        return;
    }
    if (d->mode == JD_MODE_OBD) jd_isotp_on_frame(&d->obd, f, now);
    else jd_tp20_on_frame(&d->tp, f, now);
}

// ---------------------------------------------------------------- OBD-II

static void tick_obd(jd_diag_t *d, uint32_t now)
{
    jd_isotp_tick(&d->obd, now);
    if (d->obd.state == JD_REQ_DONE) {
        size_t len;
        const uint8_t *r = jd_isotp_response(&d->obd, &len);
        got_reply(d, r, len, now);
        jd_sched_done(&d->sched, true, now);
        jd_isotp_clear(&d->obd);
    } else if (d->obd.state == JD_REQ_FAILED) {
        jd_sched_done(&d->sched, false, now);
        jd_isotp_clear(&d->obd);
    }
    if (!transmitting(d) || d->obd.state != JD_REQ_IDLE) return;
    int idx = jd_sched_next(&d->sched, now);
    if (idx < 0) return;
    if (jd_isotp_request(&d->obd, d->req[idx].bytes, d->req[idx].len, now) != JD_OK) {
        jd_sched_done(&d->sched, false, now);
        jd_isotp_clear(&d->obd);
    }
}

// ---------------------------------------------------------------- TP 2.0

static void tp_schedule_reopen(jd_diag_t *d, uint32_t now)
{
    if (d->tp_open_fails < 255) d->tp_open_fails++;
    uint32_t delay = (uint32_t)JD_DIAG_REOPEN_MIN_MS << (d->tp_open_fails > 5 ? 5 : d->tp_open_fails - 1);
    if (delay > JD_DIAG_REOPEN_MAX_MS) delay = JD_DIAG_REOPEN_MAX_MS;
    d->tp_reopen_at = now + delay;
}

static void tick_tp20(jd_diag_t *d, uint32_t now)
{
    jd_tp20_t *tp = &d->tp;
    jd_tp20_tick(tp, now);

    // Results of the request in flight
    if (tp->req_state == JD_REQ_DONE || tp->req_state == JD_REQ_FAILED) {
        bool ok = tp->req_state == JD_REQ_DONE;
        if (d->tp_session_pending) {
            d->tp_session_pending = false;
            if (!ok && TP20_INIT[d->tp_init_step].required) {
                jd_tp20_close(tp, now);
            } else if (++d->tp_init_step >= TP20_INIT_STEPS) {
                d->tp_session_ok = true;
                d->tp_open_fails = 0;
            }
        } else {
            if (ok) {
                size_t len;
                const uint8_t *r = jd_tp20_response(tp, &len);
                got_reply(d, r, len, now);
            }
            jd_sched_done(&d->sched, ok, now);
        }
        jd_tp20_clear(tp);
    }

    // Channel lost (or never came up): try again later, slower each time.
    if (tp->state == JD_TP20_CLOSED && d->tp_was_open) {
        d->tp_was_open = false;
        d->tp_session_ok = d->tp_session_pending = false;
        d->tp_init_step = 0;
        tp_schedule_reopen(d, now);
    }
    if (!transmitting(d)) return;

    if (tp->state == JD_TP20_CLOSED) {
        if (!jd_time_reached(now, d->tp_reopen_at)) return;
        if (jd_tp20_open(tp, now) == JD_OK) d->tp_was_open = true;
        else tp_schedule_reopen(d, now);
        return;
    }
    if (!jd_tp20_is_open(tp) || tp->req_state != JD_REQ_IDLE) return;
    if (!d->tp_session_ok) {
        if (!d->tp_session_pending && jd_tp20_request(tp, TP20_INIT[d->tp_init_step].bytes, 2, now) == JD_OK)
            d->tp_session_pending = true;
        return;
    }
    int idx = jd_sched_next(&d->sched, now);
    if (idx >= 0 && jd_tp20_request(tp, d->req[idx].bytes, d->req[idx].len, now) != JD_OK)
        jd_sched_done(&d->sched, false, now);
}

void jd_diag_tick(jd_diag_t *d, uint32_t now)
{
    if (d->mode == JD_MODE_OBD) tick_obd(d, now);
    else if (d->mode == JD_MODE_TP20) tick_tp20(d, now);
}

jd_link_t jd_diag_link(const jd_diag_t *d, uint32_t now)
{
    if (d->gate->halt != JD_HALT_NONE) return JD_LINK_HALTED;
    if (!transmitting(d)) return JD_LINK_LISTEN;
    if (d->any_reply && !jd_time_reached(now, d->last_reply_at + JD_DIAG_LINK_OK_MS)) return JD_LINK_OK;
    if (d->sched.quiet || (d->mode == JD_MODE_TP20 && d->tp_open_fails >= JD_DIAG_NO_ECU_AFTER))
        return JD_LINK_NO_ECU;
    return JD_LINK_CONNECTING;
}

const char *jd_link_name(jd_link_t l)
{
    switch (l) {
    case JD_LINK_LISTEN:     return "LISTEN ONLY";
    case JD_LINK_CONNECTING: return "CONNECTING";
    case JD_LINK_OK:         return "ECU OK";
    case JD_LINK_NO_ECU:     return "NO ECU";
    case JD_LINK_HALTED:     return "TX HALTED";
    }
    return "?";
}
