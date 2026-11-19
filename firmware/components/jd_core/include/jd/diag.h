// Ties everything together: the transmit gate, ISO-TP / TP 2.0 clients, poll scheduler,
// passive sniffer and value store. The firmware calls jd_diag_on_frame() for every
// received frame and jd_diag_tick() every ~10 ms, from ONE task.
#pragma once
#include "jd/isotp.h"
#include "jd/sched.h"
#include "jd/sniff.h"
#include "jd/store.h"
#include "jd/tp20.h"
#include "jd/txgate.h"

typedef enum {
    JD_MODE_PASSIVE = 0, // listen only: decode another tester's traffic (phase 2)
    JD_MODE_OBD,         // OBD-II mode 01 on 7E0 (phase 3 demo route)
    JD_MODE_TP20,        // TP 2.0 + KWP group 099
} jd_mode_t;

typedef enum {
    JD_LINK_LISTEN = 0,  // not transmitting (passive mode or listen-only build)
    JD_LINK_CONNECTING,
    JD_LINK_OK,          // replies in the last JD_DIAG_LINK_OK_MS
    JD_LINK_NO_ECU,      // backing off: ignition off / ECU not answering
    JD_LINK_HALTED,      // bus fault or guard violation: transmit stopped until reboot
} jd_link_t;

#define JD_DIAG_LINK_OK_MS      5000
#define JD_DIAG_REOPEN_MIN_MS   2000   // TP 2.0 channel reopen back-off: 2, 4, then every 5 s
#define JD_DIAG_REOPEN_MAX_MS   5000
#define JD_DIAG_NO_ECU_AFTER    3      // failed channel opens before showing "no ECU"

typedef struct {
    uint8_t bytes[JD_TP20_MAX_REQUEST];
    uint8_t len;
} jd_diag_req_t;

typedef struct {
    jd_mode_t    mode;
    jd_txgate_t *gate;
    jd_isotp_t   obd;
    jd_tp20_t    tp;
    jd_sched_t   sched;
    jd_sniff_t   sniff;
    jd_store_t   store;
    jd_diag_req_t req[JD_SCHED_MAX_ITEMS];

    // TP 2.0 channel upkeep
    bool     tp_session_ok, tp_session_pending, tp_was_open;
    uint8_t  tp_init_step;   // channel opening sequence (see diag.c TP20_INIT)
    uint32_t tp_reopen_at;
    uint8_t  tp_open_fails;

    uint32_t replies;
    uint32_t last_reply_at;
    bool     any_reply;
} jd_diag_t;

void jd_diag_init(jd_diag_t *d, jd_mode_t mode, jd_txgate_t *gate, uint32_t now);
void jd_diag_on_frame(jd_diag_t *d, const jd_frame_t *f, uint32_t now);
void jd_diag_tick(jd_diag_t *d, uint32_t now);
jd_link_t jd_diag_link(const jd_diag_t *d, uint32_t now);
const char *jd_link_name(jd_link_t l);
