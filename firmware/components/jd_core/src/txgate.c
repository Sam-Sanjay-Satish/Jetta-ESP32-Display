#include "jd/txgate.h"
#include <string.h>
#include "jd/guard.h"

void jd_txgate_init(jd_txgate_t *g, bool tx_enabled, jd_tx_fn hw_send, void *hw_ctx)
{
    memset(g, 0, sizeof *g);
    g->tx_enabled = tx_enabled;
    g->hw_send = hw_send;
    g->hw_ctx = hw_ctx;
}

void jd_txgate_bus_status(jd_txgate_t *g, const jd_bus_status_t *s)
{
    if (g->halt != JD_HALT_NONE) return; // latched: keep the first reason
    if (s->bus_off) g->halt = JD_HALT_BUS_OFF;
    else if (s->error_passive) g->halt = JD_HALT_ERROR_PASSIVE;
    else if (s->tx_errors >= JD_TEC_HALT || s->rx_errors >= JD_TEC_HALT) g->halt = JD_HALT_ERROR_WARNING;
}

bool jd_txgate_send(void *gate, const jd_frame_t *f)
{
    jd_txgate_t *g = gate;
    if (!g->tx_enabled) { g->refused_listen_only++; return false; }
    if (g->halt != JD_HALT_NONE) { g->refused_halted++; return false; }
    if (!jd_guard_check(f)) {
        g->refused_guard++;
        g->halt = JD_HALT_GUARD_VIOLATION;
        return false;
    }
    if (!g->hw_send(g->hw_ctx, f)) { g->hw_failed++; return false; }
    g->sent++;
    return true;
}

const char *jd_halt_name(jd_halt_reason_t r)
{
    switch (r) {
    case JD_HALT_NONE:            return "none";
    case JD_HALT_BUS_OFF:         return "bus off";
    case JD_HALT_ERROR_PASSIVE:   return "error passive";
    case JD_HALT_ERROR_WARNING:   return "error warning";
    case JD_HALT_GUARD_VIOLATION: return "guard violation";
    }
    return "?";
}
