// ISO-TP (ISO 15765-2) for OBD-II on the engine's physical IDs 7E0 -> 7E8.
// Non-blocking: feed it frames with jd_isotp_on_frame() and time with jd_isotp_tick().
// Requests are single-frame only (all our requests are <= 7 bytes), padded with 00.
#pragma once
#include <stddef.h>
#include "jd/can_frame.h"
#include "jd/common.h"

#define JD_ISOTP_MAX_MSG          64    // longest reply we accept (PID 78 is 11 bytes)
#define JD_ISOTP_RESPONSE_TIMEOUT 500   // ms from request to first reply frame
#define JD_ISOTP_PENDING_TIMEOUT  5000  // ms after a 7F xx 78 "response pending"
#define JD_ISOTP_CF_TIMEOUT       1000  // ms between consecutive frames (N_Cr)

// ---- Reassembler (also used by the passive sniffer, which never sends) ----
typedef enum {
    JD_ISOTP_RX_IGNORED = 0,   // not a frame for us / nothing in progress
    JD_ISOTP_RX_FIRST,         // first frame of a multi-frame message: send flow control now
    JD_ISOTP_RX_PROGRESS,      // consecutive frame accepted
    JD_ISOTP_RX_COMPLETE,      // message complete: see buf/len
    JD_ISOTP_RX_ERROR,         // malformed / out of sequence / too long; reassembly reset
} jd_isotp_rx_result_t;

typedef struct {
    uint8_t  buf[JD_ISOTP_MAX_MSG];
    uint16_t len;        // bytes received so far (or the complete length)
    uint16_t expected;   // announced length (0 = idle)
    uint8_t  next_sn;
} jd_isotp_rx_t;

void jd_isotp_rx_reset(jd_isotp_rx_t *rx);
jd_isotp_rx_result_t jd_isotp_rx_feed(jd_isotp_rx_t *rx, const uint8_t *data, uint8_t len);

// ---- Client: one request at a time ----
typedef struct {
    uint16_t       tx_id, rx_id;
    jd_tx_fn       tx;
    void          *tx_ctx;
    jd_req_state_t state;
    jd_err_t       err;
    uint8_t        nrc;       // negative response code if err == JD_ERR_NEGATIVE
    uint8_t        sid;       // service of the request in flight
    uint32_t       deadline;
    jd_isotp_rx_t  rx;
} jd_isotp_t;

void jd_isotp_init(jd_isotp_t *c, uint16_t tx_id, uint16_t rx_id, jd_tx_fn tx, void *tx_ctx);
// Sends a single-frame request. On JD_OK the state is BUSY.
jd_err_t jd_isotp_request(jd_isotp_t *c, const uint8_t *payload, uint8_t len, uint32_t now);
void jd_isotp_on_frame(jd_isotp_t *c, const jd_frame_t *f, uint32_t now);
void jd_isotp_tick(jd_isotp_t *c, uint32_t now);
// The reply once state == DONE (valid until the next request).
const uint8_t *jd_isotp_response(const jd_isotp_t *c, size_t *len);
// Back to IDLE after DONE/FAILED (a request also does this).
void jd_isotp_clear(jd_isotp_t *c);
