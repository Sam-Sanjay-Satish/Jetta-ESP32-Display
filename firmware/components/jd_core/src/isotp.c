#include "jd/isotp.h"
#include <string.h>

// ---------------------------------------------------------------- reassembler

void jd_isotp_rx_reset(jd_isotp_rx_t *rx)
{
    rx->len = 0;
    rx->expected = 0;
    rx->next_sn = 0;
}

static jd_isotp_rx_result_t rx_error(jd_isotp_rx_t *rx)
{
    jd_isotp_rx_reset(rx);
    return JD_ISOTP_RX_ERROR;
}

jd_isotp_rx_result_t jd_isotp_rx_feed(jd_isotp_rx_t *rx, const uint8_t *d, uint8_t len)
{
    if (len == 0) return JD_ISOTP_RX_IGNORED;
    switch (d[0] >> 4) {
    case 0x0: { // single frame
        uint8_t n = d[0] & 0x0F;
        if (n == 0 || n > 7 || n > len - 1) return rx_error(rx);
        memcpy(rx->buf, &d[1], n);
        rx->len = rx->expected = n;
        return JD_ISOTP_RX_COMPLETE;
    }
    case 0x1: { // first frame
        uint16_t total = (uint16_t)(((d[0] & 0x0F) << 8) | (len > 1 ? d[1] : 0));
        if (len < 8 || total < 8 || total > JD_ISOTP_MAX_MSG) return rx_error(rx);
        memcpy(rx->buf, &d[2], 6);
        rx->len = 6;
        rx->expected = total;
        rx->next_sn = 1;
        return JD_ISOTP_RX_FIRST;
    }
    case 0x2: { // consecutive frame
        if (rx->expected == 0) return JD_ISOTP_RX_IGNORED;
        if ((d[0] & 0x0F) != rx->next_sn) return rx_error(rx);
        uint16_t want = (uint16_t)(rx->expected - rx->len);
        uint16_t n = want < 7 ? want : 7;
        if (n > len - 1) return rx_error(rx);
        memcpy(&rx->buf[rx->len], &d[1], n);
        rx->len = (uint16_t)(rx->len + n);
        rx->next_sn = (uint8_t)((rx->next_sn + 1) & 0x0F);
        return rx->len == rx->expected ? JD_ISOTP_RX_COMPLETE : JD_ISOTP_RX_PROGRESS;
    }
    default:
        return JD_ISOTP_RX_IGNORED; // flow control from the ECU or junk
    }
}

// ---------------------------------------------------------------- client

// Continue, no block limit, 1 ms between frames: exactly what VCDS sent the engine (2026-11-18, 329x).
static const uint8_t FLOW_CONTROL[8] = { 0x30, 0x00, 0x01, 0, 0, 0, 0, 0 };

void jd_isotp_init(jd_isotp_t *c, uint16_t tx_id, uint16_t rx_id, jd_tx_fn tx, void *tx_ctx)
{
    memset(c, 0, sizeof *c);
    c->tx_id = tx_id;
    c->rx_id = rx_id;
    c->tx = tx;
    c->tx_ctx = tx_ctx;
}

static void fail(jd_isotp_t *c, jd_err_t err)
{
    c->state = JD_REQ_FAILED;
    c->err = err;
    jd_isotp_rx_reset(&c->rx);
}

static bool send(jd_isotp_t *c, const uint8_t data[8])
{
    jd_frame_t f = jd_frame(c->tx_id, data, 8);
    return c->tx(c->tx_ctx, &f);
}

jd_err_t jd_isotp_request(jd_isotp_t *c, const uint8_t *payload, uint8_t len, uint32_t now)
{
    if (c->state == JD_REQ_BUSY) return JD_ERR_BUSY;
    if (len == 0 || len > 7) return JD_ERR_ARG;
    jd_isotp_clear(c);
    uint8_t sf[8] = { len };
    memcpy(&sf[1], payload, len);
    c->sid = payload[0];
    if (!send(c, sf)) {
        fail(c, JD_ERR_TX_REFUSED);
        return JD_ERR_TX_REFUSED;
    }
    c->state = JD_REQ_BUSY;
    c->deadline = now + JD_ISOTP_RESPONSE_TIMEOUT;
    return JD_OK;
}

// A complete message arrived: is it the answer to our request?
static void on_message(jd_isotp_t *c, uint32_t now)
{
    const uint8_t *m = c->rx.buf;
    uint16_t n = c->rx.len;
    if (n >= 3 && m[0] == 0x7F && m[1] == c->sid) {
        if (m[2] == 0x78) { // response pending: keep waiting
            c->deadline = now + JD_ISOTP_PENDING_TIMEOUT;
            jd_isotp_rx_reset(&c->rx);
            return;
        }
        c->nrc = m[2];
        fail(c, JD_ERR_NEGATIVE);
        return;
    }
    if (n >= 1 && m[0] == (uint8_t)(c->sid + 0x40)) {
        c->state = JD_REQ_DONE;
        return;
    }
    jd_isotp_rx_reset(&c->rx); // someone else's reply: ignore
}

void jd_isotp_on_frame(jd_isotp_t *c, const jd_frame_t *f, uint32_t now)
{
    if (c->state != JD_REQ_BUSY || f->id != c->rx_id) return;
    switch (jd_isotp_rx_feed(&c->rx, f->data, f->len)) {
    case JD_ISOTP_RX_FIRST:
        if (!send(c, FLOW_CONTROL)) { fail(c, JD_ERR_TX_REFUSED); return; }
        c->deadline = now + JD_ISOTP_CF_TIMEOUT;
        break;
    case JD_ISOTP_RX_PROGRESS:
        c->deadline = now + JD_ISOTP_CF_TIMEOUT;
        break;
    case JD_ISOTP_RX_COMPLETE:
        on_message(c, now);
        break;
    case JD_ISOTP_RX_ERROR:
        fail(c, JD_ERR_PROTOCOL);
        break;
    case JD_ISOTP_RX_IGNORED:
        break;
    }
}

void jd_isotp_tick(jd_isotp_t *c, uint32_t now)
{
    if (c->state == JD_REQ_BUSY && jd_time_reached(now, c->deadline)) fail(c, JD_ERR_TIMEOUT);
}

const uint8_t *jd_isotp_response(const jd_isotp_t *c, size_t *len)
{
    if (c->state != JD_REQ_DONE) { *len = 0; return NULL; }
    *len = c->rx.len;
    return c->rx.buf;
}

void jd_isotp_clear(jd_isotp_t *c)
{
    c->state = JD_REQ_IDLE;
    c->err = JD_OK;
    c->nrc = 0;
    jd_isotp_rx_reset(&c->rx);
}
