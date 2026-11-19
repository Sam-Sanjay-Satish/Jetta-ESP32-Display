#include "jd/tp20.h"
#include <string.h>

// Opcodes (CLAUDE.md "TP 2.0 to the engine")
#define OP_SETUP_REQUEST   0xC0
#define OP_SETUP_POSITIVE  0xD0
#define OP_SETUP_NEG_MIN   0xD6
#define OP_SETUP_NEG_MAX   0xD8
#define OP_PARAMS_REQUEST  0xA0
#define OP_PARAMS_RESPONSE 0xA1
#define OP_CHANNEL_TEST    0xA3
#define OP_DISCONNECT      0xA8
#define DATA_MORE_ACK      0x0
#define DATA_LAST_ACK      0x1
#define DATA_MORE_NOACK    0x2
#define DATA_LAST_NOACK    0x3
#define ACK_NOT_READY      0x9
#define ACK_READY          0xB
#define LENGTH_FLAG        0x8000 // set on 7F xx 78 messages; masked

static const uint8_t SETUP[7]  = { JD_TP20_ENGINE_ADDR, OP_SETUP_REQUEST, 0x00, 0x10,
                                   JD_TP20_RX_ID & 0xFF, (JD_TP20_RX_ID >> 8) & 0x07, 0x01 };
static const uint8_t TIMING[5] = { 0x0F, 0x8A, 0xFF, 0x32, 0xFF }; // block size 15, T1 100 ms, T3 5 ms

// ---------------------------------------------------------------- helpers

uint32_t jd_tp20_decode_time_us(uint8_t v)
{
    static const uint32_t unit_us[4] = { 100, 1000, 10000, 100000 };
    return (uint32_t)(v & 0x3F) * unit_us[v >> 6];
}

int jd_tp20_decode_id(uint8_t lo, uint8_t hi)
{
    if (hi & 0x10) return -1;
    return ((hi & 0x07) << 8) | lo;
}

static bool send(jd_tp20_t *t, uint16_t id, const uint8_t *d, uint8_t len, uint32_t now)
{
    jd_frame_t f = jd_frame(id, d, len);
    bool ok = t->tx(t->tx_ctx, &f);
    if (ok) t->last_tx = now;
    return ok;
}

static bool send1(jd_tp20_t *t, uint8_t b, uint32_t now) { return send(t, JD_TP20_TX_ID, &b, 1, now); }

static bool send_timing(jd_tp20_t *t, uint8_t op, uint32_t now)
{
    uint8_t d[6] = { op };
    memcpy(&d[1], TIMING, sizeof TIMING);
    return send(t, JD_TP20_TX_ID, d, 6, now);
}

static void fail_request(jd_tp20_t *t, jd_err_t err)
{
    if (t->req_state != JD_REQ_BUSY) return;
    t->req_state = JD_REQ_FAILED;
    t->req_err = err;
    t->phase = JD_TP20_REQ_NONE;
}

// Closes the channel. A8 is sent only if the channel got as far as the engine's reply.
static void fail_channel(jd_tp20_t *t, jd_err_t err, bool send_a8, uint32_t now)
{
    bool was_up = t->state == JD_TP20_TIMING || t->state == JD_TP20_OPEN;
    t->state = JD_TP20_CLOSED;
    t->err = err;
    t->msg_active = false;
    t->keepalive_out = false;
    fail_request(t, err);
    if (send_a8 && was_up) send1(t, OP_DISCONNECT, now);
}

// ---------------------------------------------------------------- open / close

void jd_tp20_init(jd_tp20_t *t, jd_tx_fn tx, void *tx_ctx)
{
    memset(t, 0, sizeof *t);
    t->tx = tx;
    t->tx_ctx = tx_ctx;
    t->err = JD_ERR_CLOSED;
}

jd_err_t jd_tp20_open(jd_tp20_t *t, uint32_t now)
{
    jd_tx_fn tx = t->tx;
    void *ctx = t->tx_ctx;
    jd_tp20_init(t, tx, ctx);
    if (!send(t, JD_TP20_SETUP_ID, SETUP, sizeof SETUP, now)) {
        t->err = JD_ERR_TX_REFUSED;
        return JD_ERR_TX_REFUSED;
    }
    t->state = JD_TP20_SETUP;
    t->err = JD_OK;
    t->deadline = now + JD_TP20_SETUP_TIMEOUT;
    return JD_OK;
}

void jd_tp20_close(jd_tp20_t *t, uint32_t now)
{
    if (t->state == JD_TP20_CLOSED) return;
    fail_channel(t, JD_ERR_CLOSED, true, now);
}

static void on_setup_reply(jd_tp20_t *t, const jd_frame_t *f, uint32_t now)
{
    if (f->len < 2 || f->data[0] != 0x00) return; // not addressed to tester 00
    uint8_t op = f->data[1];
    if (op >= OP_SETUP_NEG_MIN && op <= OP_SETUP_NEG_MAX) {
        fail_channel(t, JD_ERR_NEGATIVE, false, now);
        return;
    }
    if (op != OP_SETUP_POSITIVE || f->len < 7) {
        fail_channel(t, JD_ERR_PROTOCOL, false, now);
        return;
    }
    int rx = jd_tp20_decode_id(f->data[2], f->data[3]);
    int tx = jd_tp20_decode_id(f->data[4], f->data[5]);
    if (rx != JD_TP20_RX_ID || tx != JD_TP20_TX_ID) {
        // Never transmit on an ID the allowlist doesn't name.
        fail_channel(t, JD_ERR_PROTOCOL, false, now);
        return;
    }
    if (!send_timing(t, OP_PARAMS_REQUEST, now)) {
        fail_channel(t, JD_ERR_TX_REFUSED, false, now);
        return;
    }
    t->state = JD_TP20_TIMING;
    t->deadline = now + JD_TP20_CONTROL_TIMEOUT;
}

// ---------------------------------------------------------------- requests

jd_err_t jd_tp20_request(jd_tp20_t *t, const uint8_t *payload, uint8_t len, uint32_t now)
{
    if (t->state != JD_TP20_OPEN) return JD_ERR_CLOSED;
    if (t->req_state == JD_REQ_BUSY) return JD_ERR_BUSY;
    if (len == 0 || len > JD_TP20_MAX_REQUEST) return JD_ERR_ARG;
    memcpy(t->req, payload, len);
    t->req_len = len;
    t->req_state = JD_REQ_BUSY;
    t->req_err = JD_OK;
    t->nrc = 0;
    t->retries = 0;
    t->not_ready = 0;
    t->phase = JD_TP20_REQ_QUEUED;
    jd_tp20_tick(t, now); // goes out now if the gap allows
    return JD_OK;
}

static void try_send_request(jd_tp20_t *t, uint32_t now)
{
    if (t->phase != JD_TP20_REQ_QUEUED || t->keepalive_out) return;
    if (!jd_time_reached(now, t->last_tx + JD_TP20_REQUEST_GAP)) return;
    uint8_t d[3 + JD_TP20_MAX_REQUEST] = { (uint8_t)((DATA_LAST_ACK << 4) | t->tx_seq), 0x00, t->req_len };
    memcpy(&d[3], t->req, t->req_len);
    if (!send(t, JD_TP20_TX_ID, d, (uint8_t)(3 + t->req_len), now)) {
        fail_channel(t, JD_ERR_TX_REFUSED, false, now);
        return;
    }
    t->phase = JD_TP20_REQ_WAIT_ACK;
    t->req_deadline = now + t->ack_timeout;
}

static void retry_request(jd_tp20_t *t, uint32_t now)
{
    if (++t->retries > JD_TP20_MAX_RETRIES) {
        fail_channel(t, JD_ERR_TIMEOUT, true, now);
        return;
    }
    t->phase = JD_TP20_REQ_QUEUED;
    try_send_request(t, now);
}

static void on_ack(jd_tp20_t *t, uint8_t seq, bool ready, uint32_t now)
{
    if (t->phase != JD_TP20_REQ_WAIT_ACK && t->phase != JD_TP20_REQ_WAIT_READY) return;
    uint8_t expected = (uint8_t)((t->tx_seq + 1) & 0x0F);
    if (seq == t->tx_seq) { // names our own packet: not taken
        if (ready) {
            retry_request(t, now);
            return;
        }
        if (++t->not_ready > JD_TP20_MAX_NOT_READY) { fail_channel(t, JD_ERR_TIMEOUT, true, now); return; }
        t->phase = JD_TP20_REQ_WAIT_READY;
        t->req_deadline = now + t->ack_timeout;
        return;
    }
    if (seq != expected) {
        fail_channel(t, JD_ERR_PROTOCOL, true, now);
        return;
    }
    if (!ready) { // taken, engine busy: wait for the ready ACK
        if (++t->not_ready > JD_TP20_MAX_NOT_READY) { fail_channel(t, JD_ERR_TIMEOUT, true, now); return; }
        t->req_deadline = now + t->ack_timeout;
        return;
    }
    t->tx_seq = expected;
    t->phase = JD_TP20_REQ_WAIT_REPLY;
    t->req_deadline = now + JD_TP20_RESPONSE_TIMEOUT;
}

// A whole message arrived. Is it the answer to our request?
static void on_message(jd_tp20_t *t, uint32_t now)
{
    if (t->req_state != JD_REQ_BUSY || t->phase != JD_TP20_REQ_WAIT_REPLY) return; // stale: drop
    const uint8_t *m = t->msg;
    uint8_t sid = t->req[0];
    if (t->msg_len >= 3 && m[0] == 0x7F && m[1] == sid) {
        if (m[2] == 0x78) { t->req_deadline = now + JD_TP20_PENDING_TIMEOUT; return; }
        t->nrc = m[2];
        fail_request(t, JD_ERR_NEGATIVE);
        return;
    }
    if (t->msg_len >= 1 && m[0] == (uint8_t)(sid + 0x40)) {
        t->req_state = JD_REQ_DONE;
        t->phase = JD_TP20_REQ_NONE;
    }
}

static void on_data(jd_tp20_t *t, const jd_frame_t *f, uint8_t op, uint8_t seq, uint32_t now)
{
    bool wants_ack = op == DATA_MORE_ACK || op == DATA_LAST_ACK;
    if (seq != t->rx_seq) {
        if (t->recent_len[seq] == f->len && memcmp(t->recent[seq], f->data, f->len) == 0) {
            // The engine repeated a packet because it missed our ACK: ACK again, drop it.
            if (wants_ack) send1(t, (uint8_t)((ACK_READY << 4) | t->rx_seq), now);
            return;
        }
        fail_channel(t, JD_ERR_PROTOCOL, true, now);
        return;
    }
    if (t->phase == JD_TP20_REQ_WAIT_ACK) { // data before the ACK: the ACK was lost, the request was taken
        t->tx_seq = (uint8_t)((t->tx_seq + 1) & 0x0F);
        t->phase = JD_TP20_REQ_WAIT_REPLY;
    }
    t->rx_seq = (uint8_t)((t->rx_seq + 1) & 0x0F);
    memcpy(t->recent[seq], f->data, f->len);
    t->recent_len[seq] = f->len;

    uint8_t from = 1;
    if (!t->msg_active) {
        if (f->len < 3) { fail_channel(t, JD_ERR_PROTOCOL, true, now); return; }
        uint16_t length = (uint16_t)(((f->data[1] << 8) | f->data[2]) & ~LENGTH_FLAG);
        if (length == 0 || length > JD_TP20_MAX_MSG) { fail_channel(t, JD_ERR_PROTOCOL, true, now); return; }
        t->msg_expected = length;
        t->msg_len = 0;
        t->msg_active = true;
        from = 3;
    }
    uint16_t n = (uint16_t)(f->len - from);
    if (t->msg_len + n > t->msg_expected) { fail_channel(t, JD_ERR_PROTOCOL, true, now); return; }
    memcpy(&t->msg[t->msg_len], &f->data[from], n);
    t->msg_len = (uint16_t)(t->msg_len + n);

    if (wants_ack) send1(t, (uint8_t)((ACK_READY << 4) | t->rx_seq), now);
    bool last = op == DATA_LAST_ACK || op == DATA_LAST_NOACK;
    if (!last) return;
    if (t->msg_len != t->msg_expected) { fail_channel(t, JD_ERR_PROTOCOL, true, now); return; }
    t->msg_active = false;
    on_message(t, now);
}

// ---------------------------------------------------------------- frames and time

void jd_tp20_on_frame(jd_tp20_t *t, const jd_frame_t *f, uint32_t now)
{
    if (t->state == JD_TP20_SETUP) {
        if (f->id == JD_TP20_SETUP_ID + JD_TP20_ENGINE_ADDR) on_setup_reply(t, f, now);
        return;
    }
    if (t->state == JD_TP20_CLOSED || f->id != JD_TP20_RX_ID || f->len == 0) return;

    uint8_t b0 = f->data[0];
    switch (b0) {
    case OP_DISCONNECT:
        fail_channel(t, JD_ERR_CLOSED, false, now);
        return;
    case OP_CHANNEL_TEST:
    case OP_PARAMS_REQUEST:
        send_timing(t, OP_PARAMS_RESPONSE, now);
        return;
    case OP_PARAMS_RESPONSE:
        if (t->state == JD_TP20_TIMING) {
            if (f->len < 6) { fail_channel(t, JD_ERR_PROTOCOL, true, now); return; }
            uint32_t t1_ms = jd_tp20_decode_time_us(f->data[2]) / 1000;
            t->ack_timeout = (uint16_t)(t1_ms ? t1_ms : JD_TP20_DEFAULT_ACK_MS);
            t->state = JD_TP20_OPEN;
            t->last_keepalive = now;
        } else if (t->keepalive_out) {
            t->keepalive_out = false;
            t->last_keepalive = now;
            try_send_request(t, now);
        }
        return;
    default:
        break;
    }
    if (t->state != JD_TP20_OPEN) return;
    uint8_t op = b0 >> 4, seq = b0 & 0x0F;
    switch (op) {
    case ACK_READY:     on_ack(t, seq, true, now); break;
    case ACK_NOT_READY: on_ack(t, seq, false, now); break;
    case DATA_MORE_ACK:
    case DATA_LAST_ACK:
    case DATA_MORE_NOACK:
    case DATA_LAST_NOACK: on_data(t, f, op, seq, now); break;
    default: break;
    }
}

void jd_tp20_tick(jd_tp20_t *t, uint32_t now)
{
    switch (t->state) {
    case JD_TP20_CLOSED:
        return;
    case JD_TP20_SETUP:
        if (jd_time_reached(now, t->deadline)) fail_channel(t, JD_ERR_TIMEOUT, false, now);
        return;
    case JD_TP20_TIMING:
        if (jd_time_reached(now, t->deadline)) fail_channel(t, JD_ERR_TIMEOUT, true, now);
        return;
    case JD_TP20_OPEN:
        break;
    }
    if (t->keepalive_out && jd_time_reached(now, t->deadline)) {
        fail_channel(t, JD_ERR_TIMEOUT, true, now);
        return;
    }
    if (t->req_state == JD_REQ_BUSY && t->phase != JD_TP20_REQ_QUEUED && jd_time_reached(now, t->req_deadline)) {
        if (t->phase == JD_TP20_REQ_WAIT_REPLY) {
            t->msg_active = false;
            fail_request(t, JD_ERR_TIMEOUT);
        } else {
            retry_request(t, now);
        }
        if (t->state != JD_TP20_OPEN) return;
    }
    try_send_request(t, now);
    bool idle = t->phase == JD_TP20_REQ_NONE && !t->keepalive_out;
    if (idle && jd_time_reached(now, t->last_keepalive + JD_TP20_KEEPALIVE_MS) &&
        jd_time_reached(now, t->last_tx + JD_TP20_REQUEST_GAP)) {
        if (!send1(t, OP_CHANNEL_TEST, now)) { fail_channel(t, JD_ERR_TX_REFUSED, false, now); return; }
        t->keepalive_out = true;
        t->deadline = now + JD_TP20_CONTROL_TIMEOUT;
    }
}

const uint8_t *jd_tp20_response(const jd_tp20_t *t, size_t *len)
{
    if (t->req_state != JD_REQ_DONE) { *len = 0; return NULL; }
    *len = t->msg_len;
    return t->msg;
}

void jd_tp20_clear(jd_tp20_t *t)
{
    if (t->req_state == JD_REQ_BUSY) return;
    t->req_state = JD_REQ_IDLE;
    t->req_err = JD_OK;
    t->phase = JD_TP20_REQ_NONE;
}
