#include "jd/decode.h"

static jd_value_t val(int32_t v, uint8_t decimals, jd_unit_t unit)
{
    return (jd_value_t){ .value = v, .decimals = decimals, .unit = unit, .valid = true };
}

static const jd_value_t NO_VALUE = { 0 };

// n / d rounded half away from zero (d > 0).
static int32_t div_round(int32_t n, int32_t d)
{
    return n >= 0 ? (n + d / 2) / d : -((-n + d / 2) / d);
}

// Formulas confirmed against VCDS (CLAUDE.md). Anything else: no value.
jd_value_t jd_kwp_decode(uint8_t formula, uint8_t a, uint8_t b)
{
    int32_t A = a, B = b;
    switch (formula) {
    case 0x01: return val(2 * A * B, 1, JD_UNIT_RPM);                       // 0.2*a*b
    case 0x05: return val(A * (B - 100), 1, JD_UNIT_C);                     // a*(b-100)*0.1
    case 0x07: return val(A * B, 2, JD_UNIT_KMH);                           // 0.01*a*b
    case 0x14: return val(div_round(A * (B - 128) * 100, 128), 2, JD_UNIT_PCT); // a*(b-128)/128
    case 0x21: return A ? val(div_round(1000 * B, A), 1, JD_UNIT_PCT) : NO_VALUE; // 100*b/a
    case 0x27: return val(div_round(A * B * 100, 255), 2, JD_UNIT_MG_STR);  // a*b/255
    case 0x31: return val(div_round(A * B * 100, 40), 2, JD_UNIT_MG_STR);   // a*b/40
    case 0x60: return val(A * B, 1, JD_UNIT_MBAR);                          // a*b/10
    case 0x64: return val(A * B, 1, JD_UNIT_BAR);                           // a*b/10
    default:   return NO_VALUE;                                             // incl. 25 00 00 (blank)
    }
}

bool jd_kwp_parse_group(const uint8_t *msg, size_t len, uint8_t group, jd_value_t out[JD_KWP_FIELDS])
{
    if (!msg || len < 2 + 3 * JD_KWP_FIELDS || msg[0] != 0x61 || msg[1] != group) return false;
    for (int i = 0; i < JD_KWP_FIELDS; i++) {
        const uint8_t *t = &msg[2 + 3 * i];
        out[i] = jd_kwp_decode(t[0], t[1], t[2]);
    }
    return true;
}

int jd_kwp_parse_dtcs(const uint8_t *msg, size_t len, jd_dtc_t *out, int max)
{
    if (!msg || len < 2 || msg[0] != 0x58) return -1;
    int count = msg[1];
    if (len != 2 + 3 * (size_t)count) return -1;
    for (int i = 0; i < count && i < max; i++) {
        const uint8_t *r = &msg[2 + 3 * i];
        out[i] = (jd_dtc_t){ .code = (uint16_t)((r[0] << 8) | r[1]), .status = r[2] };
    }
    return count;
}

void jd_dtc_pcode(uint16_t code, char out[6])
{
    static const char letter[4] = { 'P', 'C', 'B', 'U' };
    static const char hex[] = "0123456789ABCDEF";
    out[0] = letter[code >> 14];
    out[1] = hex[(code >> 12) & 0x3];
    out[2] = hex[(code >> 8) & 0xF];
    out[3] = hex[(code >> 4) & 0xF];
    out[4] = hex[code & 0xF];
    out[5] = 0;
}

bool jd_obd_decode(const uint8_t *msg, size_t len, uint8_t pid, jd_value_t *out)
{
    if (!msg || len < 3 || msg[0] != 0x41 || msg[1] != pid) return false;
    switch (pid) {
    case 0x05: *out = val((int32_t)msg[2] - 40, 0, JD_UNIT_C); return true;
    case 0x0D: *out = val(msg[2], 0, JD_UNIT_KMH); return true;
    case 0x0C:
        if (len < 4) return false;
        *out = val(((int32_t)msg[2] * 256 + msg[3]) * 25, 2, JD_UNIT_RPM); // (256A+B)/4
        return true;
    default:
        return false;
    }
}

bool jd_obd_egt_sensor(const uint8_t *msg, size_t len, int sensor, jd_value_t *out)
{
    if (!msg || sensor < 1 || sensor > 4) return false;
    size_t hi = 3 + 2 * (size_t)(sensor - 1);
    if (len < hi + 2 || msg[0] != 0x41 || msg[1] != 0x78) return false;
    if (!(msg[2] & (1u << (sensor - 1)))) return false;
    int32_t raw = (int32_t)msg[hi] * 256 + msg[hi + 1];
    *out = val(raw - 400, 1, JD_UNIT_C); // raw/10 - 40
    return true;
}
