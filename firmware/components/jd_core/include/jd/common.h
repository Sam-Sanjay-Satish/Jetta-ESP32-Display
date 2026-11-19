// Shared result codes and wrap-safe millisecond time helpers.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    JD_OK = 0,
    JD_ERR_BUSY,        // a request is already in flight
    JD_ERR_ARG,         // bad request (e.g. too long)
    JD_ERR_TX_REFUSED,  // the send path refused the frame (guard, listen-only, bus)
    JD_ERR_TIMEOUT,     // no (complete) reply in time
    JD_ERR_NEGATIVE,    // ECU answered 7F <sid> <nrc>
    JD_ERR_PROTOCOL,    // malformed / out-of-sequence reply
    JD_ERR_CLOSED,      // channel not open, or closed by the ECU
} jd_err_t;

typedef enum {
    JD_REQ_IDLE = 0,
    JD_REQ_BUSY,
    JD_REQ_DONE,
    JD_REQ_FAILED,
} jd_req_state_t;

const char *jd_err_name(jd_err_t e);

// true once `now` has reached `deadline` (works across the 49-day uint32 wrap).
static inline bool jd_time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}
