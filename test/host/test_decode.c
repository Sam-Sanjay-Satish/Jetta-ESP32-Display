// Value decoding, checked against VCDS's own readings from the 2026-11-18 captures.
#include "jd/decode.h"
#include "test_util.h"

void setUp(void) {}
void tearDown(void) {}

#define ASSERT_VALUE(v_, val, dec, unit_) do { \
        jd_value_t v__ = (v_); \
        TEST_ASSERT_TRUE_MESSAGE(v__.valid, "value not valid"); \
        TEST_ASSERT_EQUAL_INT32(val, v__.value); \
        TEST_ASSERT_EQUAL_UINT8(dec, v__.decimals); \
        TEST_ASSERT_EQUAL_INT(unit_, v__.unit); \
    } while (0)

// ---- KWP formulas ----

static void test_formula_05_temperature(void)
{
    ASSERT_VALUE(jd_kwp_decode(0x05, 0x09, 0x8F), 387, 1, JD_UNIT_C);   // fuel 38.7
    ASSERT_VALUE(jd_kwp_decode(0x05, 0x09, 0xAF), 675, 1, JD_UNIT_C);   // coolant 67.5
    ASSERT_VALUE(jd_kwp_decode(0x05, 0x3C, 0x7F), 1620, 1, JD_UNIT_C);  // EGT 162.0
    ASSERT_VALUE(jd_kwp_decode(0x05, 0x3C, 0x78), 1200, 1, JD_UNIT_C);  // 120.0
    ASSERT_VALUE(jd_kwp_decode(0x05, 0x0A, 0x00), -1000, 1, JD_UNIT_C); // below zero
}

static void test_formula_01_rpm(void)
{
    ASSERT_VALUE(jd_kwp_decode(0x01, 0x73, 0x24), 8280, 1, JD_UNIT_RPM); // 828
}

static void test_other_confirmed_formulas(void)
{
    ASSERT_VALUE(jd_kwp_decode(0x07, 0x64, 0x32), 5000, 2, JD_UNIT_KMH);  // 0.01*100*50 = 50.00
    ASSERT_VALUE(jd_kwp_decode(0x14, 0x80, 0xC0), 6400, 2, JD_UNIT_PCT);  // 128*(192-128)/128 = 64.00
    ASSERT_VALUE(jd_kwp_decode(0x14, 0x80, 0x40), -6400, 2, JD_UNIT_PCT); // negative
    ASSERT_VALUE(jd_kwp_decode(0x21, 0xC8, 0x64), 500, 1, JD_UNIT_PCT);   // 100*100/200 = 50.0
    ASSERT_VALUE(jd_kwp_decode(0x27, 0xFF, 0x0A), 1000, 2, JD_UNIT_MG_STR); // 255*10/255 = 10.00
    ASSERT_VALUE(jd_kwp_decode(0x31, 0x28, 0x0A), 1000, 2, JD_UNIT_MG_STR); // 40*10/40 = 10.00
    ASSERT_VALUE(jd_kwp_decode(0x60, 0xFA, 0x28), 10000, 1, JD_UNIT_MBAR);  // 250*40/10 = 1000.0
    ASSERT_VALUE(jd_kwp_decode(0x64, 0xFA, 0x28), 10000, 1, JD_UNIT_BAR);
}

static void test_empty_and_unknown_formulas_are_not_values(void)
{
    TEST_ASSERT_FALSE(jd_kwp_decode(0x25, 0x00, 0x00).valid); // VCDS blank (oil temp in group 007)
    TEST_ASSERT_FALSE(jd_kwp_decode(0x30, 0x00, 0x00).valid); // placeholder
    TEST_ASSERT_FALSE(jd_kwp_decode(0x1F, 0x12, 0x34).valid); // not confirmed: don't guess
    TEST_ASSERT_FALSE(jd_kwp_decode(0x32, 0x12, 0x34).valid);
    TEST_ASSERT_FALSE(jd_kwp_decode(0x21, 0x00, 0x10).valid); // division by zero
}

// ---- Whole group replies ----

static const uint8_t group_099[] = { 0x61, 0x63,
    0x01, 0x73, 0x24,  0x05, 0x3C, 0x7F,  0x05, 0x3C, 0x79,  0x05, 0x3C, 0x78,
    0x25, 0x00, 0x00,  0x25, 0x00, 0x00,  0x30, 0x00, 0x00,  0x30, 0x00, 0x00 };

static const uint8_t group_007[] = { 0x61, 0x07,
    0x05, 0x09, 0x8F,  0x25, 0x00, 0x00,  0x05, 0x09, 0x83,  0x05, 0x09, 0xAF,
    0x25, 0x00, 0x00,  0x25, 0x00, 0x00,  0x30, 0x00, 0x00,  0x30, 0x00, 0x00 };

static void test_group_099_egt(void)
{
    jd_value_t f[JD_KWP_FIELDS];
    TEST_ASSERT_TRUE(jd_kwp_parse_group(group_099, sizeof group_099, 0x63, f));
    ASSERT_VALUE(f[0], 8280, 1, JD_UNIT_RPM);
    ASSERT_VALUE(f[1], 1620, 1, JD_UNIT_C);  // EGT before turbo
    ASSERT_VALUE(f[2], 1260, 1, JD_UNIT_C);
    ASSERT_VALUE(f[3], 1200, 1, JD_UNIT_C);
}

static void test_group_007_oil_temp_is_empty(void)
{
    jd_value_t f[JD_KWP_FIELDS];
    TEST_ASSERT_TRUE(jd_kwp_parse_group(group_007, sizeof group_007, 0x07, f));
    ASSERT_VALUE(f[0], 387, 1, JD_UNIT_C);
    TEST_ASSERT_FALSE(f[1].valid);
    ASSERT_VALUE(f[2], 279, 1, JD_UNIT_C);
    ASSERT_VALUE(f[3], 675, 1, JD_UNIT_C);
}

static void test_group_reply_checks(void)
{
    jd_value_t f[JD_KWP_FIELDS];
    TEST_ASSERT_FALSE(jd_kwp_parse_group(group_099, sizeof group_099, 0x07, f)); // other group
    TEST_ASSERT_FALSE(jd_kwp_parse_group(group_099, 13, 0x63, f));              // < 4 triplets
    const uint8_t negative[] = { 0x7F, 0x21, 0x31 };
    TEST_ASSERT_FALSE(jd_kwp_parse_group(negative, sizeof negative, 0x63, f));
    TEST_ASSERT_FALSE(jd_kwp_parse_group(NULL, 0, 0x63, f));
    // Exactly 4 triplets is enough.
    TEST_ASSERT_TRUE(jd_kwp_parse_group(group_099, 14, 0x63, f));
}

// ---- OBD-II ----

static void test_obd_coolant_rpm_speed(void)
{
    jd_value_t v;
    const uint8_t coolant[] = { 0x41, 0x05, 0x6B };       // 107 - 40 = 67
    TEST_ASSERT_TRUE(jd_obd_decode(coolant, sizeof coolant, 0x05, &v));
    ASSERT_VALUE(v, 67, 0, JD_UNIT_C);
    const uint8_t rpm[] = { 0x41, 0x0C, 0x0C, 0xF0 };     // 3312 / 4 = 828
    TEST_ASSERT_TRUE(jd_obd_decode(rpm, sizeof rpm, 0x0C, &v));
    ASSERT_VALUE(v, 82800, 2, JD_UNIT_RPM);
    const uint8_t speed[] = { 0x41, 0x0D, 0x32 };
    TEST_ASSERT_TRUE(jd_obd_decode(speed, sizeof speed, 0x0D, &v));
    ASSERT_VALUE(v, 50, 0, JD_UNIT_KMH);
}

static void test_obd_reply_checks(void)
{
    jd_value_t v;
    const uint8_t coolant[] = { 0x41, 0x05, 0x6B };
    TEST_ASSERT_FALSE(jd_obd_decode(coolant, sizeof coolant, 0x0C, &v)); // other PID
    TEST_ASSERT_FALSE(jd_obd_decode(coolant, 2, 0x05, &v));              // too short
    const uint8_t neg[] = { 0x7F, 0x01, 0x12 };
    TEST_ASSERT_FALSE(jd_obd_decode(neg, sizeof neg, 0x05, &v));
    const uint8_t rpm_short[] = { 0x41, 0x0C, 0x0C };
    TEST_ASSERT_FALSE(jd_obd_decode(rpm_short, sizeof rpm_short, 0x0C, &v));
    TEST_ASSERT_FALSE(jd_obd_decode(coolant, sizeof coolant, 0x78, &v)); // use jd_obd_egt_sensor
}

static const uint8_t pid78[] = { 0x41, 0x78, 0x0F, 0x08, 0x4F, 0x07, 0x08, 0x06, 0xF2, 0x06, 0x2B };

static void test_obd_egt_from_capture(void)
{
    jd_value_t v;
    TEST_ASSERT_TRUE(jd_obd_egt_sensor(pid78, sizeof pid78, 1, &v));
    ASSERT_VALUE(v, 1727, 1, JD_UNIT_C);
    TEST_ASSERT_TRUE(jd_obd_egt_sensor(pid78, sizeof pid78, 2, &v));
    ASSERT_VALUE(v, 1400, 1, JD_UNIT_C);
    TEST_ASSERT_TRUE(jd_obd_egt_sensor(pid78, sizeof pid78, 3, &v));
    ASSERT_VALUE(v, 1378, 1, JD_UNIT_C);
    TEST_ASSERT_TRUE(jd_obd_egt_sensor(pid78, sizeof pid78, 4, &v));
    ASSERT_VALUE(v, 1179, 1, JD_UNIT_C);
}

static void test_obd_egt_checks(void)
{
    jd_value_t v;
    uint8_t only_s1[sizeof pid78];
    memcpy(only_s1, pid78, sizeof pid78);
    only_s1[2] = 0x01;
    TEST_ASSERT_TRUE(jd_obd_egt_sensor(only_s1, sizeof only_s1, 1, &v));
    TEST_ASSERT_FALSE(jd_obd_egt_sensor(only_s1, sizeof only_s1, 2, &v)); // marked absent
    TEST_ASSERT_FALSE(jd_obd_egt_sensor(pid78, 7, 3, &v));                // truncated
    TEST_ASSERT_FALSE(jd_obd_egt_sensor(pid78, sizeof pid78, 0, &v));
    TEST_ASSERT_FALSE(jd_obd_egt_sensor(pid78, sizeof pid78, 5, &v));
}

// ---- KWP fault codes: the engine's 12 stored DTCs from the 2026-11-13 capture ----

static const uint8_t dtc_reply[] = { 0x58, 0x0C,
    0x2B, 0xA6, 0xE0,  0x1A, 0x58, 0x60,  0x19, 0x46, 0x20,  0x04, 0x01, 0x61,
    0x24, 0x0F, 0xE0,  0x06, 0x91, 0x20,  0x05, 0x44, 0x20,  0x04, 0x01, 0x22,
    0x05, 0x44, 0x21,  0x00, 0x87, 0x21,  0x00, 0x87, 0x62,  0x00, 0x87, 0x26 };

static void test_dtc_list_from_capture(void)
{
    jd_dtc_t d[16];
    TEST_ASSERT_EQUAL(12, jd_kwp_parse_dtcs(dtc_reply, sizeof dtc_reply, d, 16));
    char p[8];
    jd_dtc_pcode(d[0].code, p);  TEST_ASSERT_EQUAL_STRING("P2BA6", p);
    jd_dtc_pcode(d[3].code, p);  TEST_ASSERT_EQUAL_STRING("P0401", p);
    jd_dtc_pcode(d[11].code, p); TEST_ASSERT_EQUAL_STRING("P0087", p);
    TEST_ASSERT_EQUAL(11174, d[0].code);  // VCDS's number: 011174
    // VCDS: P2BA6-000 MIL ON | P1946-000 Intermittent | P0401-001 neither | P0087-006 Intermittent
    TEST_ASSERT_TRUE(jd_dtc_mil(d[0].status));           TEST_ASSERT_FALSE(jd_dtc_intermittent(d[0].status));
    TEST_ASSERT_EQUAL(0, jd_dtc_subtype(d[0].status));
    TEST_ASSERT_FALSE(jd_dtc_mil(d[2].status));          TEST_ASSERT_TRUE(jd_dtc_intermittent(d[2].status));
    TEST_ASSERT_FALSE(jd_dtc_mil(d[3].status));          TEST_ASSERT_FALSE(jd_dtc_intermittent(d[3].status));
    TEST_ASSERT_EQUAL(1, jd_dtc_subtype(d[3].status));
    TEST_ASSERT_TRUE(jd_dtc_intermittent(d[11].status)); TEST_ASSERT_EQUAL(6, jd_dtc_subtype(d[11].status));
}

static void test_dtc_list_checks(void)
{
    jd_dtc_t d[4];
    const uint8_t none[] = { 0x58, 0x00 };
    TEST_ASSERT_EQUAL(0, jd_kwp_parse_dtcs(none, sizeof none, d, 4));
    TEST_ASSERT_EQUAL(12, jd_kwp_parse_dtcs(dtc_reply, sizeof dtc_reply, d, 4)); // reports 12, stores 4
    TEST_ASSERT_EQUAL_HEX16(0x0401, d[3].code);
    TEST_ASSERT_EQUAL(-1, jd_kwp_parse_dtcs(dtc_reply, sizeof dtc_reply - 1, d, 4)); // count/length mismatch
    const uint8_t neg[] = { 0x7F, 0x18, 0x31 };
    TEST_ASSERT_EQUAL(-1, jd_kwp_parse_dtcs(neg, sizeof neg, d, 4));
    TEST_ASSERT_EQUAL(-1, jd_kwp_parse_dtcs(NULL, 0, d, 4));
}

static void test_pcode_letters(void)
{
    // SAE J2012: top 2 bits pick P/C/B/U (only P was seen on this car).
    char p[8];
    jd_dtc_pcode(0x4123, p); TEST_ASSERT_EQUAL_STRING("C0123", p);
    jd_dtc_pcode(0x9001, p); TEST_ASSERT_EQUAL_STRING("B1001", p);
    jd_dtc_pcode(0xC155, p); TEST_ASSERT_EQUAL_STRING("U0155", p);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_formula_05_temperature);
    RUN_TEST(test_formula_01_rpm);
    RUN_TEST(test_other_confirmed_formulas);
    RUN_TEST(test_empty_and_unknown_formulas_are_not_values);
    RUN_TEST(test_group_099_egt);
    RUN_TEST(test_group_007_oil_temp_is_empty);
    RUN_TEST(test_group_reply_checks);
    RUN_TEST(test_obd_coolant_rpm_speed);
    RUN_TEST(test_obd_reply_checks);
    RUN_TEST(test_obd_egt_from_capture);
    RUN_TEST(test_obd_egt_checks);
    RUN_TEST(test_dtc_list_from_capture);
    RUN_TEST(test_dtc_list_checks);
    RUN_TEST(test_pcode_letters);
    return UNITY_END();
}
