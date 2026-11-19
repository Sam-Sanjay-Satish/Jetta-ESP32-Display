// Passive decoder for listen-only mode: rebuilds the engine's replies to another
// tester (VCDS / macvagdiag) from overheard frames. Never sends anything; never ACKs.
#pragma once
#include <stddef.h>
#include "jd/can_frame.h"
#include "jd/isotp.h"

#define JD_SNIFF_OBD_RX_ID   0x7E8   // engine's OBD-II replies
#define JD_SNIFF_TP20_RX_ID  0x300   // engine -> tester TP 2.0 channel (as negotiated by VCDS)
#define JD_SNIFF_MAX_MSG     128

typedef struct {
    jd_isotp_rx_t obd;
    uint8_t  msg[JD_SNIFF_MAX_MSG];
    uint16_t len, expected;
    bool     active;
    uint8_t  last[8];
    uint8_t  last_len;
    uint32_t frames, messages;
} jd_sniff_t;

void jd_sniff_init(jd_sniff_t *s);
// Returns true when a whole engine reply is complete; *msg/*len point into s.
bool jd_sniff_feed(jd_sniff_t *s, const jd_frame_t *f, const uint8_t **msg, size_t *len);
