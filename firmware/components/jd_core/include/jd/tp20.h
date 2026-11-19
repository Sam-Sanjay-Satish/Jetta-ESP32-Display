// VW TP 2.0 channel to the engine ECU (logical address 01), KWP2000 inside.
// Ported from macvagdiag's Tp20Channel.java, made non-blocking and allocation-free.
//
// Bytes, IDs and timing are from the 2026-11-13/18 captures and macvagdiag's real
// transmit of 2026-11-17 (CLAUDE.md "TP 2.0 to the engine"). Requests are single-packet
// only (<= 5 bytes), which is all the allowlist permits.
#pragma once
#include <stddef.h>
#include "jd/can_frame.h"
#include "jd/common.h"

#define JD_TP20_SETUP_ID          0x200
#define JD_TP20_ENGINE_ADDR       0x01
#define JD_TP20_TX_ID             0x740 // the only tester ID the allowlist permits
#define JD_TP20_RX_ID             0x300 // what we ask the engine to send on

#define JD_TP20_MAX_MSG           128   // longest reply we accept (group reply = 26 bytes)
#define JD_TP20_MAX_REQUEST       5     // fits in one packet
#define JD_TP20_SETUP_TIMEOUT     200   // ms
#define JD_TP20_CONTROL_TIMEOUT   200   // ms, A1 after A0/A3
#define JD_TP20_DEFAULT_ACK_MS    100   // if the engine's T1 decodes to 0
#define JD_TP20_RESPONSE_TIMEOUT  1000  // ms from the engine's ACK to its answer
#define JD_TP20_PENDING_TIMEOUT   5000  // ms after 7F xx 78
#define JD_TP20_REQUEST_GAP       20    // ms after our last frame before a new message (VCDS 18-27 ms)
#define JD_TP20_KEEPALIVE_MS      500   // A3 every 0.5 s, also while polling (VCDS: 0.49-0.64 s, median 0.55)
#define JD_TP20_MAX_RETRIES       2     // resends of an unacknowledged request
#define JD_TP20_MAX_NOT_READY     10    // "not ready" ACKs in a row before giving up

typedef enum {
    JD_TP20_CLOSED = 0,
    JD_TP20_SETUP,          // 200 sent, waiting for 201
    JD_TP20_TIMING,         // A0 sent, waiting for A1
    JD_TP20_OPEN,           // ready (a request may be in flight, see req_state)
} jd_tp20_state_t;

typedef enum {
    JD_TP20_REQ_NONE = 0,
    JD_TP20_REQ_QUEUED,     // waiting for the request gap
    JD_TP20_REQ_WAIT_ACK,   // sent, waiting for Bn
    JD_TP20_REQ_WAIT_READY, // engine said 9x (not taken): resend once it says Bx
    JD_TP20_REQ_WAIT_REPLY, // ACKed, waiting for the answer
} jd_tp20_req_phase_t;

typedef struct {
    jd_tx_fn        tx;
    void           *tx_ctx;
    jd_tp20_state_t state;
    jd_err_t        err;         // why the channel last closed
    uint32_t        deadline;    // setup / timing / keep-alive reply
    uint32_t        last_tx;     // when we last put a frame on the bus
    uint32_t        last_keepalive;
    bool            keepalive_out;
    uint16_t        ack_timeout;
    uint8_t         tx_seq, rx_seq;

    // Request in flight
    jd_req_state_t      req_state;
    jd_tp20_req_phase_t phase;
    jd_err_t            req_err;
    uint8_t             nrc;
    uint8_t             req[JD_TP20_MAX_REQUEST];
    uint8_t             req_len;
    uint8_t             retries, not_ready;
    uint32_t            req_deadline;

    // Message being received
    uint8_t  msg[JD_TP20_MAX_MSG];
    uint16_t msg_len, msg_expected;
    bool     msg_active;
    uint8_t  recent[16][8];      // last packet seen with each sequence number (repeat detection)
    uint8_t  recent_len[16];
} jd_tp20_t;

void jd_tp20_init(jd_tp20_t *t, jd_tx_fn tx, void *tx_ctx);
// Starts channel setup. Poll jd_tp20_is_open() / t->state.
jd_err_t jd_tp20_open(jd_tp20_t *t, uint32_t now);
void jd_tp20_close(jd_tp20_t *t, uint32_t now);
static inline bool jd_tp20_is_open(const jd_tp20_t *t) { return t->state == JD_TP20_OPEN; }

// Queues a KWP request (e.g. {0x21, 0x63}); it goes out once the request gap allows.
jd_err_t jd_tp20_request(jd_tp20_t *t, const uint8_t *payload, uint8_t len, uint32_t now);
const uint8_t *jd_tp20_response(const jd_tp20_t *t, size_t *len);
void jd_tp20_clear(jd_tp20_t *t); // request state back to IDLE after DONE/FAILED

void jd_tp20_on_frame(jd_tp20_t *t, const jd_frame_t *f, uint32_t now);
void jd_tp20_tick(jd_tp20_t *t, uint32_t now);

// Exposed for tests.
uint32_t jd_tp20_decode_time_us(uint8_t v);  // bits 7-6 unit (0.1/1/10/100 ms), bits 5-0 count
int      jd_tp20_decode_id(uint8_t lo, uint8_t hi); // -1 if marked invalid (0x10)
