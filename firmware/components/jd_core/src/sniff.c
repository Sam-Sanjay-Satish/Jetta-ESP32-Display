#include "jd/sniff.h"
#include <string.h>

void jd_sniff_init(jd_sniff_t *s)
{
    memset(s, 0, sizeof *s);
    jd_isotp_rx_reset(&s->obd);
}

static bool tp20_feed(jd_sniff_t *s, const jd_frame_t *f, const uint8_t **msg, size_t *len)
{
    if (f->len == 0) return false;
    uint8_t op = f->data[0] >> 4;
    if (op > 0x3) return false; // ACKs and channel control
    if (s->last_len == f->len && memcmp(s->last, f->data, f->len) == 0) return false; // repeat
    memcpy(s->last, f->data, f->len);
    s->last_len = f->len;

    uint8_t from = 1;
    if (!s->active) {
        if (f->len < 3) return false;
        uint16_t length = (uint16_t)(((f->data[1] << 8) | f->data[2]) & 0x7FFF);
        if (length == 0 || length > JD_SNIFF_MAX_MSG) return false; // not a message start we can use
        s->expected = length;
        s->len = 0;
        s->active = true;
        from = 3;
    }
    uint16_t n = (uint16_t)(f->len - from);
    if (s->len + n > s->expected) { s->active = false; return false; }
    memcpy(&s->msg[s->len], &f->data[from], n);
    s->len = (uint16_t)(s->len + n);
    bool last = op == 0x1 || op == 0x3;
    if (!last) return false;
    s->active = false;
    if (s->len != s->expected) return false;
    *msg = s->msg;
    *len = s->len;
    return true;
}

bool jd_sniff_feed(jd_sniff_t *s, const jd_frame_t *f, const uint8_t **msg, size_t *len)
{
    s->frames++;
    bool done = false;
    if (f->id == JD_SNIFF_OBD_RX_ID) {
        if (jd_isotp_rx_feed(&s->obd, f->data, f->len) == JD_ISOTP_RX_COMPLETE) {
            *msg = s->obd.buf;
            *len = s->obd.len;
            done = true;
        }
    } else if (f->id == JD_SNIFF_TP20_RX_ID) {
        done = tp20_feed(s, f, msg, len);
    }
    if (done) s->messages++;
    return done;
}
