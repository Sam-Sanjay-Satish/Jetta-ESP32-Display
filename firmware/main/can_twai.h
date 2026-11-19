// TWAI (CAN) controller on GPIO2/3 at 500 kbit/s. Listen-only unless transmit is
// allowed at build time. can_hw_send() is NOT for direct use: it is the hardware
// end of jd_txgate (the only transmit path, see jd/txgate.h).
#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "jd/can_frame.h"
#include "jd/txgate.h"

typedef struct {
    uint32_t rx_frames, rx_dropped;   // dropped = our queue was full
    uint32_t rx_missed, rx_overrun;   // from the controller
    uint32_t tx_errors, rx_errors, bus_errors;
} can_stats_t;

// normal_mode = false: TWAI_MODE_LISTEN_ONLY (no transmit, no ACK). Received frames
// are posted to `rx_queue` (items of jd_frame_t).
esp_err_t can_start(bool normal_mode, QueueHandle_t rx_queue);
bool can_hw_send(void *ctx, const jd_frame_t *f);
void can_bus_status(jd_bus_status_t *out, can_stats_t *stats);
