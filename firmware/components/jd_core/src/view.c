#include "jd/view.h"
#include <stdio.h>
#include <string.h>

static const jd_thresholds_t THRESHOLDS[JD_SIG_COUNT] = {
    [JD_SIG_EGT]     = { JD_EGT_WARN_C, JD_EGT_CRIT_C, JD_LEVEL_HYST_C },
    [JD_SIG_COOLANT] = { JD_COOLANT_WARN_C, JD_COOLANT_CRIT_C, JD_LEVEL_HYST_C },
    [JD_SIG_OIL]     = { JD_OIL_WARN_C, JD_OIL_CRIT_C, JD_LEVEL_HYST_C },
    [JD_SIG_RPM]     = { 0, 0, 0 },
    [JD_SIG_BOOST_SPEC] = { 0, 0, 0 },
    [JD_SIG_BOOST_ACT]  = { 0, 0, 0 },
};

const jd_thresholds_t *jd_view_thresholds(jd_signal_t sig)
{
    return sig < JD_SIG_COUNT ? &THRESHOLDS[sig] : &THRESHOLDS[JD_SIG_RPM];
}

jd_level_t jd_level_eval(jd_level_t prev, int32_t whole, const jd_thresholds_t *t)
{
    if (t->warn == 0 && t->crit == 0) return JD_LEVEL_OK;
    if (whole >= t->crit) return JD_LEVEL_CRIT;
    if (prev == JD_LEVEL_CRIT && whole >= t->crit - t->hysteresis) return JD_LEVEL_CRIT;
    if (whole >= t->warn) return JD_LEVEL_WARN;
    if (prev >= JD_LEVEL_WARN && whole >= t->warn - t->hysteresis) return JD_LEVEL_WARN;
    return JD_LEVEL_OK;
}

int32_t jd_value_whole(const jd_value_t *v)
{
    int32_t d = 1;
    for (int i = 0; i < v->decimals; i++) d *= 10;
    if (d == 1) return v->value;
    return v->value >= 0 ? (v->value + d / 2) / d : -((-v->value + d / 2) / d);
}

bool jd_view_number(const jd_store_t *s, jd_signal_t sig, uint32_t now, int32_t *out)
{
    const jd_reading_t *r = jd_store_get(s, sig, now);
    if (!r) return false;
    int32_t whole = jd_value_whole(&r->value);
    if (sig == JD_SIG_BOOST_SPEC || sig == JD_SIG_BOOST_ACT) whole -= JD_BOOST_ATMOS_MBAR;
    *out = whole;
    return true;
}

void jd_view_init(jd_view_t *v) { memset(v, 0, sizeof *v); }

void jd_view_gauge(jd_view_t *v, const jd_store_t *s, jd_signal_t sig, uint32_t now, jd_gauge_view_t *out)
{
    int32_t whole;
    if (!jd_view_number(s, sig, now, &whole)) {
        strcpy(out->text, "--");
        out->level = v->last[sig] = JD_LEVEL_NONE;
        return;
    }
    snprintf(out->text, sizeof out->text, "%ld", (long)whole);
    out->level = v->last[sig] = jd_level_eval(v->last[sig], whole, jd_view_thresholds(sig));
}
