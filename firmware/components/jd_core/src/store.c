#include "jd/store.h"
#include <string.h>
#include "jd/common.h"

void jd_store_init(jd_store_t *s) { memset(s, 0, sizeof *s); }

static int put(jd_store_t *s, jd_signal_t sig, const jd_value_t *v, uint32_t now)
{
    if (!v->valid) return 0;
    s->r[sig] = (jd_reading_t){ .value = *v, .at = now, .seen = true };
    s->updates++;
    return 1;
}

int jd_store_apply(jd_store_t *s, const uint8_t *msg, size_t len, uint32_t now)
{
    if (!msg || len < 2) return 0;
    jd_value_t f[JD_KWP_FIELDS], v;
    if (msg[0] == 0x61) {
        if (jd_kwp_parse_group(msg, len, 0x63, f)) // group 099: RPM, EGT before turbo, ...
            return put(s, JD_SIG_RPM, &f[0], now) + put(s, JD_SIG_EGT, &f[1], now);
        if (jd_kwp_parse_group(msg, len, 0x07, f)) // group 007: fuel, oil (empty here), IAT, coolant
            return put(s, JD_SIG_OIL, &f[1], now) + put(s, JD_SIG_COOLANT, &f[3], now);
        if (jd_kwp_parse_group(msg, len, 0x0B, f)) // group 011: RPM, boost specified, actual, duty
            return put(s, JD_SIG_RPM, &f[0], now) + put(s, JD_SIG_BOOST_SPEC, &f[1], now) +
                   put(s, JD_SIG_BOOST_ACT, &f[2], now);
        return 0;
    }
    if (msg[0] == 0x41) {
        switch (msg[1]) {
        case 0x05: return jd_obd_decode(msg, len, 0x05, &v) ? put(s, JD_SIG_COOLANT, &v, now) : 0;
        case 0x0C: return jd_obd_decode(msg, len, 0x0C, &v) ? put(s, JD_SIG_RPM, &v, now) : 0;
        // Sensor 1 is expected to be pre-turbo (UNCONFIRMED: compare with group 099 on the car).
        case 0x78: return jd_obd_egt_sensor(msg, len, 1, &v) ? put(s, JD_SIG_EGT, &v, now) : 0;
        default:   return 0;
        }
    }
    return 0;
}

const jd_reading_t *jd_store_get(const jd_store_t *s, jd_signal_t sig, uint32_t now)
{
    if (sig >= JD_SIG_COUNT) return NULL;
    const jd_reading_t *r = &s->r[sig];
    if (!r->seen || jd_time_reached(now, r->at + JD_STORE_STALE_MS)) return NULL;
    return r;
}
