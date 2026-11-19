// Store, view model (text + levels) and the passive sniffer.
#include "jd/sniff.h"
#include "jd/store.h"
#include "jd/view.h"
#include "test_util.h"

static jd_store_t st;
static jd_view_t  vw;

void setUp(void) { jd_store_init(&st); jd_view_init(&vw); }
void tearDown(void) {}

static const uint8_t group_099[] = { 0x61, 0x63,
    0x01, 0x73, 0x24,  0x05, 0x3C, 0x7F,  0x05, 0x3C, 0x79,  0x05, 0x3C, 0x78,
    0x25, 0x00, 0x00,  0x25, 0x00, 0x00,  0x30, 0x00, 0x00,  0x30, 0x00, 0x00 };
static const uint8_t group_007[] = { 0x61, 0x07,
    0x05, 0x09, 0x8F,  0x25, 0x00, 0x00,  0x05, 0x09, 0x83,  0x05, 0x09, 0xAF,
    0x25, 0x00, 0x00,  0x25, 0x00, 0x00,  0x30, 0x00, 0x00,  0x30, 0x00, 0x00 };

// ---------------------------------------------------------------- store

static void test_store_group_099(void)
{
    TEST_ASSERT_EQUAL(2, jd_store_apply(&st, group_099, sizeof group_099, 100));
    const jd_reading_t *egt = jd_store_get(&st, JD_SIG_EGT, 100);
    TEST_ASSERT_NOT_NULL(egt);
    TEST_ASSERT_EQUAL(1620, egt->value.value);
    TEST_ASSERT_EQUAL(8280, jd_store_get(&st, JD_SIG_RPM, 100)->value.value);
}

static void test_store_group_007_coolant_but_no_oil(void)
{
    TEST_ASSERT_EQUAL(1, jd_store_apply(&st, group_007, sizeof group_007, 0));
    TEST_ASSERT_EQUAL(675, jd_store_get(&st, JD_SIG_COOLANT, 0)->value.value);
    TEST_ASSERT_NULL(jd_store_get(&st, JD_SIG_OIL, 0)); // field 2 is empty on this ECU
}

// Group 011 layout from the 2026-11-18 capture: RPM, boost specified, boost actual (formula 60,
// mbar absolute), duty cycle. Byte values here are constructed (60 FA 28 = 250*40/10 = 1000.0 mbar).
static const uint8_t group_011[] = { 0x61, 0x0B,
    0x01, 0x73, 0x24,  0x60, 0xFA, 0x5C,  0x60, 0xFA, 0x50,  0x21, 0xFF, 0x80,
    0x25, 0x00, 0x00,  0x25, 0x00, 0x00,  0x30, 0x00, 0x00,  0x30, 0x00, 0x00 };

static void test_store_group_011_boost(void)
{
    TEST_ASSERT_EQUAL(3, jd_store_apply(&st, group_011, sizeof group_011, 0));
    const jd_reading_t *spec = jd_store_get(&st, JD_SIG_BOOST_SPEC, 0);
    const jd_reading_t *act = jd_store_get(&st, JD_SIG_BOOST_ACT, 0);
    TEST_ASSERT_NOT_NULL(spec);
    TEST_ASSERT_NOT_NULL(act);
    TEST_ASSERT_EQUAL(23000, spec->value.value); // 250 * 92 / 10 = 2300.0 mbar
    TEST_ASSERT_EQUAL(20000, act->value.value);  // 250 * 80 / 10 = 2000.0 mbar
    TEST_ASSERT_EQUAL(JD_UNIT_MBAR, act->value.unit);
    TEST_ASSERT_EQUAL(8280, jd_store_get(&st, JD_SIG_RPM, 0)->value.value);
}

static void test_no_levels_for_boost(void)
{
    jd_gauge_view_t g;
    jd_store_apply(&st, group_011, sizeof group_011, 0);
    jd_view_gauge(&vw, &st, JD_SIG_BOOST_SPEC, 0, &g);
    TEST_ASSERT_EQUAL_STRING("1287", g.text); // gauge pressure: 2300 absolute - 1013
    TEST_ASSERT_EQUAL(JD_LEVEL_OK, g.level);
}

static void test_view_number_boost_is_gauge_pressure(void)
{
    int32_t n = 0;
    TEST_ASSERT_FALSE(jd_view_number(&st, JD_SIG_BOOST_ACT, 0, &n)); // no data yet
    jd_store_apply(&st, group_011, sizeof group_011, 0);
    TEST_ASSERT_TRUE(jd_view_number(&st, JD_SIG_BOOST_ACT, 0, &n));
    TEST_ASSERT_EQUAL(2000 - JD_BOOST_ATMOS_MBAR, n);
    TEST_ASSERT_FALSE(jd_view_number(&st, JD_SIG_BOOST_ACT, JD_STORE_STALE_MS, &n)); // stale
    jd_store_apply(&st, group_007, sizeof group_007, 0);
    TEST_ASSERT_TRUE(jd_view_number(&st, JD_SIG_COOLANT, 0, &n));
    TEST_ASSERT_EQUAL(68, n); // temperatures unchanged (67.5 rounds to 68)
}

static void test_store_obd(void)
{
    const uint8_t c[] = { 0x41, 0x05, 0x6B };
    const uint8_t e[] = { 0x41, 0x78, 0x0F, 0x08, 0x4F, 0x07, 0x08, 0x06, 0xF2, 0x06, 0x2B };
    TEST_ASSERT_EQUAL(1, jd_store_apply(&st, c, sizeof c, 0));
    TEST_ASSERT_EQUAL(1, jd_store_apply(&st, e, sizeof e, 0));
    TEST_ASSERT_EQUAL(67, jd_store_get(&st, JD_SIG_COOLANT, 0)->value.value);
    TEST_ASSERT_EQUAL(1727, jd_store_get(&st, JD_SIG_EGT, 0)->value.value);
}

static void test_store_ignores_other_messages(void)
{
    const uint8_t neg[] = { 0x7F, 0x21, 0x31 };
    const uint8_t session[] = { 0x50, 0x89 };
    TEST_ASSERT_EQUAL(0, jd_store_apply(&st, neg, sizeof neg, 0));
    TEST_ASSERT_EQUAL(0, jd_store_apply(&st, session, sizeof session, 0));
    TEST_ASSERT_EQUAL(0, jd_store_apply(&st, NULL, 0, 0));
    TEST_ASSERT_EQUAL(0, st.updates);
}

static void test_store_staleness(void)
{
    jd_store_apply(&st, group_099, sizeof group_099, 1000);
    TEST_ASSERT_NOT_NULL(jd_store_get(&st, JD_SIG_EGT, 1000 + JD_STORE_STALE_MS - 1));
    TEST_ASSERT_NULL(jd_store_get(&st, JD_SIG_EGT, 1000 + JD_STORE_STALE_MS));
}

// ---------------------------------------------------------------- view

static void test_whole_rounding(void)
{
    jd_value_t v = { .value = 1625, .decimals = 1, .valid = true };
    TEST_ASSERT_EQUAL(163, jd_value_whole(&v));
    v.value = 1624; TEST_ASSERT_EQUAL(162, jd_value_whole(&v));
    v.value = -55;  TEST_ASSERT_EQUAL(-6, jd_value_whole(&v));
    v.value = 82800; v.decimals = 2; TEST_ASSERT_EQUAL(828, jd_value_whole(&v));
    v.value = 67; v.decimals = 0; TEST_ASSERT_EQUAL(67, jd_value_whole(&v));
}

static void test_gauge_text_and_level(void)
{
    jd_store_apply(&st, group_099, sizeof group_099, 0);
    jd_gauge_view_t g;
    jd_view_gauge(&vw, &st, JD_SIG_EGT, 0, &g);
    TEST_ASSERT_EQUAL_STRING("162", g.text);
    TEST_ASSERT_EQUAL(JD_LEVEL_OK, g.level);
}

static void test_gauge_no_data(void)
{
    jd_gauge_view_t g;
    jd_view_gauge(&vw, &st, JD_SIG_OIL, 0, &g);
    TEST_ASSERT_EQUAL_STRING("--", g.text);
    TEST_ASSERT_EQUAL(JD_LEVEL_NONE, g.level);
    jd_store_apply(&st, group_099, sizeof group_099, 0);
    jd_view_gauge(&vw, &st, JD_SIG_EGT, JD_STORE_STALE_MS, &g); // stale
    TEST_ASSERT_EQUAL_STRING("--", g.text);
}

static void test_levels_with_hysteresis(void)
{
    const jd_thresholds_t *t = jd_view_thresholds(JD_SIG_EGT);
    jd_level_t l = JD_LEVEL_NONE;
    l = jd_level_eval(l, JD_EGT_WARN_C - 1, t); TEST_ASSERT_EQUAL(JD_LEVEL_OK, l);
    l = jd_level_eval(l, JD_EGT_WARN_C, t);     TEST_ASSERT_EQUAL(JD_LEVEL_WARN, l);
    l = jd_level_eval(l, JD_EGT_WARN_C - 5, t); TEST_ASSERT_EQUAL(JD_LEVEL_WARN, l); // inside band
    l = jd_level_eval(l, JD_EGT_WARN_C - JD_LEVEL_HYST_C, t); TEST_ASSERT_EQUAL(JD_LEVEL_WARN, l);
    l = jd_level_eval(l, JD_EGT_WARN_C - JD_LEVEL_HYST_C - 1, t); TEST_ASSERT_EQUAL(JD_LEVEL_OK, l);
    l = jd_level_eval(l, JD_EGT_CRIT_C, t);     TEST_ASSERT_EQUAL(JD_LEVEL_CRIT, l);
    l = jd_level_eval(l, JD_EGT_CRIT_C - 5, t); TEST_ASSERT_EQUAL(JD_LEVEL_CRIT, l);
    l = jd_level_eval(l, JD_EGT_CRIT_C - 20, t); TEST_ASSERT_EQUAL(JD_LEVEL_WARN, l);
    l = jd_level_eval(l, 100, t);               TEST_ASSERT_EQUAL(JD_LEVEL_OK, l);
    // Straight from OK to CRIT and back down past both bands
    l = jd_level_eval(JD_LEVEL_OK, 900, t);     TEST_ASSERT_EQUAL(JD_LEVEL_CRIT, l);
    l = jd_level_eval(l, 50, t);                TEST_ASSERT_EQUAL(JD_LEVEL_OK, l);
}

static void test_no_levels_for_rpm(void)
{
    const jd_thresholds_t *t = jd_view_thresholds(JD_SIG_RPM);
    TEST_ASSERT_EQUAL(JD_LEVEL_OK, jd_level_eval(JD_LEVEL_NONE, 5000, t));
}

static void test_view_remembers_level(void)
{
    // 705 C -> WARN; then 695 C (inside the band) stays WARN on screen.
    uint8_t hot[] = { 0x61, 0x63, 0x01, 0x73, 0x24, 0x05, 0x46, 0xC8, 0x25, 0, 0, 0x25, 0, 0 }; // 70*(200-100)=7000 -> 700
    jd_gauge_view_t g;
    jd_store_apply(&st, hot, sizeof hot, 0);
    jd_view_gauge(&vw, &st, JD_SIG_EGT, 0, &g);
    TEST_ASSERT_EQUAL_STRING("700", g.text);
    TEST_ASSERT_EQUAL(JD_LEVEL_WARN, g.level);
    hot[7] = 0xC7; // 70*99 = 6930 -> 693
    jd_store_apply(&st, hot, sizeof hot, 10);
    jd_view_gauge(&vw, &st, JD_SIG_EGT, 10, &g);
    TEST_ASSERT_EQUAL_STRING("693", g.text);
    TEST_ASSERT_EQUAL(JD_LEVEL_WARN, g.level);
}

// ---------------------------------------------------------------- sniffer

static jd_sniff_t sn;

static int feed(uint16_t id, const char *hex, const uint8_t **msg, size_t *len)
{
    jd_frame_t f = F(id, hex);
    return jd_sniff_feed(&sn, &f, msg, len);
}

static void test_sniff_vcds_group_099(void)
{
    // What VCDS and the engine exchanged (2026-11-18, idle), as heard on the bus.
    jd_sniff_init(&sn);
    const uint8_t *m; size_t n;
    TEST_ASSERT_FALSE(feed(0x740, "1C 00 02 21 63", &m, &n)); // tester's request: ignored
    TEST_ASSERT_FALSE(feed(0x300, "BD", &m, &n));             // engine ACK
    TEST_ASSERT_FALSE(feed(0x300, "2A 00 1A 61 63 01 73 24", &m, &n));
    TEST_ASSERT_FALSE(feed(0x300, "2B 05 3C 7F 05 3C 79 05", &m, &n));
    TEST_ASSERT_FALSE(feed(0x300, "2B 05 3C 7F 05 3C 79 05", &m, &n)); // repeat: dropped
    TEST_ASSERT_FALSE(feed(0x300, "2C 3C 78 25 00 00 25 00", &m, &n));
    TEST_ASSERT_TRUE(feed(0x300, "1D 00 30 00 00 30 00 00", &m, &n));
    TEST_ASSERT_EQUAL(26, n);
    TEST_ASSERT_EQUAL(2, jd_store_apply(&st, m, n, 0));
    TEST_ASSERT_EQUAL(1620, jd_store_get(&st, JD_SIG_EGT, 0)->value.value);
    TEST_ASSERT_FALSE(feed(0x740, "BE", &m, &n));
}

static void test_sniff_obd_multi_frame(void)
{
    jd_sniff_init(&sn);
    const uint8_t *m; size_t n;
    TEST_ASSERT_FALSE(feed(0x7DF, "02 01 78 00 00 00 00 00", &m, &n));
    TEST_ASSERT_FALSE(feed(0x7E8, "10 0B 41 78 0F 08 4F 07", &m, &n));
    TEST_ASSERT_FALSE(feed(0x7E0, "30 00 01 00 00 00 00 00", &m, &n)); // VCDS's flow control
    TEST_ASSERT_TRUE(feed(0x7E8, "21 08 06 F2 06 2B 00 00", &m, &n));
    TEST_ASSERT_EQUAL(11, n);
    TEST_ASSERT_EQUAL(1, jd_store_apply(&st, m, n, 0));
    TEST_ASSERT_FALSE(feed(0x7E9, "03 41 05 50 00 00 00 00", &m, &n)); // DSG: ignored
}

static void test_sniff_resyncs_after_garbage(void)
{
    jd_sniff_init(&sn);
    const uint8_t *m; size_t n;
    TEST_ASSERT_FALSE(feed(0x300, "21 05 3C 7F 05 3C 79 05", &m, &n));   // middle of a message we missed
    TEST_ASSERT_FALSE(feed(0x300, "20 7F FF 61 63 01 73 24", &m, &n));   // absurd length: ignored
    TEST_ASSERT_TRUE(feed(0x300, "13 00 02 50 89", &m, &n));             // next whole message is fine
    TEST_ASSERT_EQUAL(2, n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_store_group_099);
    RUN_TEST(test_store_group_007_coolant_but_no_oil);
    RUN_TEST(test_store_group_011_boost);
    RUN_TEST(test_no_levels_for_boost);
    RUN_TEST(test_view_number_boost_is_gauge_pressure);
    RUN_TEST(test_store_obd);
    RUN_TEST(test_store_ignores_other_messages);
    RUN_TEST(test_store_staleness);
    RUN_TEST(test_whole_rounding);
    RUN_TEST(test_gauge_text_and_level);
    RUN_TEST(test_gauge_no_data);
    RUN_TEST(test_levels_with_hysteresis);
    RUN_TEST(test_no_levels_for_rpm);
    RUN_TEST(test_view_remembers_level);
    RUN_TEST(test_sniff_vcds_group_099);
    RUN_TEST(test_sniff_obd_multi_frame);
    RUN_TEST(test_sniff_resyncs_after_garbage);
    return UNITY_END();
}
