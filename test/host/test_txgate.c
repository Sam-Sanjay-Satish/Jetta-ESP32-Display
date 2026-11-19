// The single transmit path: listen-only, latched bus halt, allowlist.
#include "jd/txgate.h"
#include "test_util.h"

static tx_log_t    hw;
static jd_txgate_t g;

void setUp(void)
{
    memset(&hw, 0, sizeof hw);
    jd_txgate_init(&g, true, tx_log_send, &hw);
}
void tearDown(void) {}

static bool send(uint16_t id, const char *hex)
{
    jd_frame_t f = F(id, hex);
    return jd_txgate_send(&g, &f);
}

static void test_allowed_frame_goes_out(void)
{
    TEST_ASSERT_TRUE(send(0x7E0, "02 01 05 00 00 00 00 00"));
    TEST_ASSERT_EQUAL(1, hw.count);
    TEST_ASSERT_EQUAL(1, g.sent);
}

static void test_listen_only_sends_nothing(void)
{
    jd_txgate_init(&g, false, tx_log_send, &hw);
    TEST_ASSERT_FALSE(send(0x7E0, "02 01 05 00 00 00 00 00"));
    TEST_ASSERT_EQUAL(0, hw.count);
    TEST_ASSERT_EQUAL(1, g.refused_listen_only);
    TEST_ASSERT_FALSE(jd_txgate_can_send(&g));
}

static void test_forbidden_frame_refused_and_latches_halt(void)
{
    TEST_ASSERT_FALSE(send(0x7E0, "01 04 00 00 00 00 00 00")); // clear codes
    TEST_ASSERT_EQUAL(0, hw.count);
    TEST_ASSERT_EQUAL(JD_HALT_GUARD_VIOLATION, g.halt);
    // Everything after a violation is refused, even allowed frames.
    TEST_ASSERT_FALSE(send(0x7E0, "02 01 05 00 00 00 00 00"));
    TEST_ASSERT_EQUAL(0, hw.count);
}

static void test_bus_off_latches(void)
{
    jd_txgate_bus_status(&g, &(jd_bus_status_t){ .bus_off = true });
    TEST_ASSERT_EQUAL(JD_HALT_BUS_OFF, g.halt);
    jd_txgate_bus_status(&g, &(jd_bus_status_t){ 0 }); // recovered: still halted
    TEST_ASSERT_EQUAL(JD_HALT_BUS_OFF, g.halt);
    TEST_ASSERT_FALSE(send(0x7E0, "02 01 05 00 00 00 00 00"));
    TEST_ASSERT_EQUAL(1, g.refused_halted);
}

static void test_error_passive_and_warning_latch(void)
{
    jd_txgate_bus_status(&g, &(jd_bus_status_t){ .error_passive = true });
    TEST_ASSERT_EQUAL(JD_HALT_ERROR_PASSIVE, g.halt);

    jd_txgate_init(&g, true, tx_log_send, &hw);
    jd_txgate_bus_status(&g, &(jd_bus_status_t){ .tx_errors = JD_TEC_HALT - 1 });
    TEST_ASSERT_EQUAL(JD_HALT_NONE, g.halt);
    jd_txgate_bus_status(&g, &(jd_bus_status_t){ .tx_errors = JD_TEC_HALT });
    TEST_ASSERT_EQUAL(JD_HALT_ERROR_WARNING, g.halt);

    jd_txgate_init(&g, true, tx_log_send, &hw);
    jd_txgate_bus_status(&g, &(jd_bus_status_t){ .rx_errors = JD_TEC_HALT });
    TEST_ASSERT_EQUAL(JD_HALT_ERROR_WARNING, g.halt);
}

static void test_first_halt_reason_kept(void)
{
    jd_txgate_bus_status(&g, &(jd_bus_status_t){ .error_passive = true });
    jd_txgate_bus_status(&g, &(jd_bus_status_t){ .bus_off = true });
    TEST_ASSERT_EQUAL(JD_HALT_ERROR_PASSIVE, g.halt);
}

static void test_hardware_failure_counted(void)
{
    hw.refuse = true;
    TEST_ASSERT_FALSE(send(0x7E0, "02 01 05 00 00 00 00 00"));
    TEST_ASSERT_EQUAL(1, g.hw_failed);
    TEST_ASSERT_EQUAL(JD_HALT_NONE, g.halt); // a full TX queue isn't a bus fault
}

static void test_halt_names(void)
{
    TEST_ASSERT_EQUAL_STRING("guard violation", jd_halt_name(JD_HALT_GUARD_VIOLATION));
    TEST_ASSERT_EQUAL_STRING("none", jd_halt_name(JD_HALT_NONE));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_allowed_frame_goes_out);
    RUN_TEST(test_listen_only_sends_nothing);
    RUN_TEST(test_forbidden_frame_refused_and_latches_halt);
    RUN_TEST(test_bus_off_latches);
    RUN_TEST(test_error_passive_and_warning_latch);
    RUN_TEST(test_first_halt_reason_kept);
    RUN_TEST(test_hardware_failure_counted);
    RUN_TEST(test_halt_names);
    return UNITY_END();
}
