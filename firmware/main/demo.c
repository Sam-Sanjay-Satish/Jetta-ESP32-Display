#include "demo.h"
#include <math.h>

static uint32_t s_rng = 12345;
static float    s_act = 1000.0f;   // actual boost follows specified with a lag
static uint32_t s_last_ms;
static bool     s_started;

static float noise(float amp) // -amp .. +amp
{
    s_rng = s_rng * 1103515245u + 12345u;
    return amp * ((float)((s_rng >> 16) & 0x7FFF) / 16383.5f - 1.0f);
}

static float hump(float t, float from, float to) // 0 -> 1 -> 0 between from and to
{
    if (t <= from || t >= to) return 0.0f;
    float s = sinf(3.14159265f * (t - from) / (to - from));
    return s * s;
}

static void put(jd_store_t *s, jd_signal_t sig, float v, jd_unit_t unit, uint32_t now)
{
    s->r[sig] = (jd_reading_t){
        .value = { .value = (int32_t)lroundf(v * 10.0f), .decimals = 1, .unit = unit, .valid = true },
        .at = now, .seen = true,
    };
    s->updates++;
}

void demo_fill(jd_store_t *s, uint32_t now)
{
    float dt = s_started ? (float)(now - s_last_ms) / 1000.0f : 0.0f;
    s_started = true;
    s_last_ms = now;
    float t = (float)(now % DEMO_LOOP_MS) / 1000.0f;

    // Temperatures: warm-up curve, then a hot spell between 110 s and 165 s.
    float cool = 22.0f + 68.0f * (1.0f - expf(-t / 22.0f)) + 24.0f * hump(t, 110.0f, 165.0f);
    float oil = 22.0f + 78.0f * (1.0f - expf(-t / 40.0f)) + 42.0f * hump(t, 105.0f, 170.0f);
    put(s, JD_SIG_COOLANT, cool + noise(0.2f), JD_UNIT_C, now);
    put(s, JD_SIG_OIL, oil + noise(0.2f), JD_UNIT_C, now);

    // Boost (mbar absolute, like the ECU; the screen subtracts the atmosphere): idle ~1010, a 3 s pull every 7 s to a target that varies.
    int pulse = (int)(t / 7.0f);
    float p = t - 7.0f * (float)pulse;
    float target = 1700.0f + (float)((pulse * 37) % 8) * 100.0f; // 1700..2400
    float spec = 1010.0f;
    if (p > 1.0f && p < 4.0f) spec = target - 150.0f * (p - 1.0f) / 3.0f; // tapers a bit at high rpm
    else if (p >= 4.0f && p < 4.5f) spec = 1010.0f + (target - 1160.0f) * (4.5f - p) * 2.0f;
    float goal = (pulse % 4 == 3 && spec > 1100.0f) ? spec - 0.3f * (spec - 1010.0f) : spec;
    float k = dt / 0.45f; // first-order lag, time constant 0.45 s (turbo spool)
    s_act += (goal - s_act) * (k > 1.0f ? 1.0f : k);
    put(s, JD_SIG_BOOST_SPEC, spec, JD_UNIT_MBAR, now);
    put(s, JD_SIG_BOOST_ACT, s_act + noise(12.0f), JD_UNIT_MBAR, now);
    put(s, JD_SIG_RPM, 820.0f + (spec - 1010.0f) * 1.4f + noise(8.0f), JD_UNIT_RPM, now);
}
