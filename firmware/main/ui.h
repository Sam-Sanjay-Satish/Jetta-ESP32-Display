// Gauge screen (LVGL 8.4, page itself in gauges.c). Runs in its own task; the diag task hands it snapshots.
// Only labels whose text or colour changed are redrawn, so the SPI bus and CPU stay idle
// most of the time.
#pragma once
#include "jd/diag.h"

typedef struct {
    jd_store_t       store;
    jd_link_t        link;
    jd_mode_t        mode;
    bool             tx_enabled;
    bool             can_ok;
    bool             demo;      // store holds fake values (menuconfig "Demo mode")
    jd_halt_reason_t halt;
    uint32_t         rx_frames, replies, tx_sent, rx_errors, tx_errors;
} ui_snapshot_t;

void ui_start(void);
void ui_publish(const ui_snapshot_t *s); // thread-safe copy
