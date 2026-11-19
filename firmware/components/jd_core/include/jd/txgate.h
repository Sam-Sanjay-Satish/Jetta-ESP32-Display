// The ONE path from the diagnostic code to the CAN transmit call (HARD RULES 1, 4, 7).
// A frame goes out only if transmit is enabled (not listen-only), the bus is healthy
// (latched halt on error-passive / bus-off / a guard violation), and the allowlist
// accepts it. Pass jd_txgate_send as the jd_tx_fn of the ISO-TP / TP 2.0 clients.
#pragma once
#include "jd/can_frame.h"

// Bus state as reported by the CAN controller.
typedef struct {
    bool     bus_off;
    bool     error_passive;
    uint32_t tx_errors;  // TEC
    uint32_t rx_errors;  // REC
} jd_bus_status_t;

#define JD_TEC_HALT 96   // error-warning level: stop before the controller goes error-passive

typedef enum {
    JD_HALT_NONE = 0,
    JD_HALT_BUS_OFF,
    JD_HALT_ERROR_PASSIVE,
    JD_HALT_ERROR_WARNING,
    JD_HALT_GUARD_VIOLATION, // something tried to send a forbidden frame: a bug, stop for good
} jd_halt_reason_t;

typedef struct {
    bool             tx_enabled;   // false = listen-only (rule 7)
    jd_halt_reason_t halt;         // latched until reboot
    jd_tx_fn         hw_send;      // the real transmit (TWAI), or a test double
    void            *hw_ctx;
    uint32_t         sent, refused_listen_only, refused_halted, refused_guard, hw_failed;
} jd_txgate_t;

void jd_txgate_init(jd_txgate_t *g, bool tx_enabled, jd_tx_fn hw_send, void *hw_ctx);
// Feed the controller's status regularly; a bad bus latches a halt.
void jd_txgate_bus_status(jd_txgate_t *g, const jd_bus_status_t *s);
// jd_tx_fn-compatible: ctx is the jd_txgate_t.
bool jd_txgate_send(void *gate, const jd_frame_t *f);
static inline bool jd_txgate_can_send(const jd_txgate_t *g) { return g->tx_enabled && g->halt == JD_HALT_NONE; }
const char *jd_halt_name(jd_halt_reason_t r);
