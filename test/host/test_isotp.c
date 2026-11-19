// ISO-TP client for OBD-II on 7E0 -> 7E8.
#include "jd/guard.h"
#include "jd/isotp.h"
#include "test_util.h"

static tx_log_t   txl;
static jd_isotp_t c;

void setUp(void)
{
    memset(&txl, 0, sizeof txl);
    jd_isotp_init(&c, 0x7E0, 0x7E8, tx_log_send, &txl);
}
void tearDown(void) {}

static void rx(uint32_t now, const char *hex)
{
    jd_frame_t f = F(0x7E8, hex);
    jd_isotp_on_frame(&c, &f, now);
}

static const uint8_t PID05[] = { 0x01, 0x05 };
static const uint8_t PID78[] = { 0x01, 0x78 };

static void test_request_is_padded_single_frame(void)
{
    TEST_ASSERT_EQUAL(JD_OK, jd_isotp_request(&c, PID05, 2, 0));
    TEST_ASSERT_EQUAL(1, txl.count);
    ASSERT_SENT(&txl, 0, 0x7E0, "02 01 05 00 00 00 00 00");
    TEST_ASSERT_EQUAL(JD_REQ_BUSY, c.state);
}

static void test_single_frame_reply(void)
{
    jd_isotp_request(&c, PID05, 2, 0);
    rx(12, "03 41 05 6B 00 00 00 00");
    TEST_ASSERT_EQUAL(JD_REQ_DONE, c.state);
    size_t n;
    const uint8_t *r = jd_isotp_response(&c, &n);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(((uint8_t[]){ 0x41, 0x05, 0x6B }), r, 3);
}

static void test_other_ids_ignored(void)
{
    jd_isotp_request(&c, PID05, 2, 0);
    jd_frame_t dsg = F(0x7E9, "03 41 05 50 00 00 00 00");
    jd_isotp_on_frame(&c, &dsg, 5);
    TEST_ASSERT_EQUAL(JD_REQ_BUSY, c.state);
}

static void test_multi_frame_pid78_from_capture(void)
{
    jd_isotp_request(&c, PID78, 2, 0);
    rx(10, "10 0B 41 78 0F 08 4F 07");
    TEST_ASSERT_EQUAL(2, txl.count);
    ASSERT_SENT(&txl, 1, 0x7E0, "30 00 01 00 00 00 00 00"); // VCDS: 30 00 01 (2026-11-18)
    TEST_ASSERT_EQUAL(JD_REQ_BUSY, c.state);
    rx(11, "21 08 06 F2 06 2B 00 00");
    TEST_ASSERT_EQUAL(JD_REQ_DONE, c.state);
    size_t n;
    const uint8_t *r = jd_isotp_response(&c, &n);
    const uint8_t expected[] = { 0x41, 0x78, 0x0F, 0x08, 0x4F, 0x07, 0x08, 0x06, 0xF2, 0x06, 0x2B };
    TEST_ASSERT_EQUAL(sizeof expected, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, r, n);
}

static void test_long_multi_frame_message(void)
{
    // 60 bytes: first frame (6) + 8 consecutive frames, through the reassembler directly.
    jd_isotp_rx_t r;
    jd_isotp_rx_reset(&r);
    uint8_t ff[] = { 0x10, 60, 1, 2, 3, 4, 5, 6 };
    TEST_ASSERT_EQUAL(JD_ISOTP_RX_FIRST, jd_isotp_rx_feed(&r, ff, 8));
    int sn = 1;
    jd_isotp_rx_result_t res = JD_ISOTP_RX_PROGRESS;
    for (int i = 0; i < 8; i++) {          // 6 + 7*8 = 62 >= 60
        uint8_t cf[8] = { (uint8_t)(0x20 | (sn & 0x0F)), 7, 7, 7, 7, 7, 7, 7 };
        res = jd_isotp_rx_feed(&r, cf, 8);
        sn++;
    }
    TEST_ASSERT_EQUAL(JD_ISOTP_RX_COMPLETE, res);
    TEST_ASSERT_EQUAL(60, r.len);
}

static void test_wrong_sequence_fails(void)
{
    jd_isotp_request(&c, PID78, 2, 0);
    rx(10, "10 0B 41 78 0F 08 4F 07");
    rx(11, "22 08 06 F2 06 2B 00 00");
    TEST_ASSERT_EQUAL(JD_REQ_FAILED, c.state);
    TEST_ASSERT_EQUAL(JD_ERR_PROTOCOL, c.err);
}

static void test_too_long_reply_fails(void)
{
    jd_isotp_request(&c, PID78, 2, 0);
    rx(10, "10 FF 41 78 0F 08 4F 07");
    TEST_ASSERT_EQUAL(JD_REQ_FAILED, c.state);
    TEST_ASSERT_EQUAL(JD_ERR_PROTOCOL, c.err);
    TEST_ASSERT_EQUAL(1, txl.count); // no flow control for a message we can't hold
}

static void test_timeout_without_reply(void)
{
    jd_isotp_request(&c, PID05, 2, 1000);
    jd_isotp_tick(&c, 1000 + JD_ISOTP_RESPONSE_TIMEOUT - 1);
    TEST_ASSERT_EQUAL(JD_REQ_BUSY, c.state);
    jd_isotp_tick(&c, 1000 + JD_ISOTP_RESPONSE_TIMEOUT);
    TEST_ASSERT_EQUAL(JD_REQ_FAILED, c.state);
    TEST_ASSERT_EQUAL(JD_ERR_TIMEOUT, c.err);
}

static void test_timeout_between_consecutive_frames(void)
{
    jd_isotp_request(&c, PID78, 2, 0);
    rx(10, "10 0B 41 78 0F 08 4F 07");
    jd_isotp_tick(&c, 10 + JD_ISOTP_CF_TIMEOUT - 1);
    TEST_ASSERT_EQUAL(JD_REQ_BUSY, c.state);
    jd_isotp_tick(&c, 10 + JD_ISOTP_CF_TIMEOUT);
    TEST_ASSERT_EQUAL(JD_ERR_TIMEOUT, c.err);
}

static void test_time_wraparound(void)
{
    uint32_t t0 = 0xFFFFFF00u;
    jd_isotp_request(&c, PID05, 2, t0);
    jd_isotp_tick(&c, t0 + 300); // wrapped past zero, still before the deadline
    TEST_ASSERT_EQUAL(JD_REQ_BUSY, c.state);
    jd_isotp_tick(&c, t0 + JD_ISOTP_RESPONSE_TIMEOUT);
    TEST_ASSERT_EQUAL(JD_REQ_FAILED, c.state);
}

static void test_response_pending_extends_wait(void)
{
    jd_isotp_request(&c, PID05, 2, 0);
    rx(10, "03 7F 01 78 00 00 00 00");
    TEST_ASSERT_EQUAL(JD_REQ_BUSY, c.state);
    jd_isotp_tick(&c, 10 + JD_ISOTP_RESPONSE_TIMEOUT + 100);
    TEST_ASSERT_EQUAL(JD_REQ_BUSY, c.state);
    rx(2000, "03 41 05 6B 00 00 00 00");
    TEST_ASSERT_EQUAL(JD_REQ_DONE, c.state);
}

static void test_negative_response(void)
{
    jd_isotp_request(&c, PID05, 2, 0);
    rx(10, "03 7F 01 12 00 00 00 00");
    TEST_ASSERT_EQUAL(JD_REQ_FAILED, c.state);
    TEST_ASSERT_EQUAL(JD_ERR_NEGATIVE, c.err);
    TEST_ASSERT_EQUAL_HEX8(0x12, c.nrc);
}

static void test_busy_and_bad_requests(void)
{
    uint8_t long_req[8] = { 0 };
    TEST_ASSERT_EQUAL(JD_ERR_ARG, jd_isotp_request(&c, long_req, 8, 0));
    TEST_ASSERT_EQUAL(JD_ERR_ARG, jd_isotp_request(&c, long_req, 0, 0));
    TEST_ASSERT_EQUAL(0, txl.count);
    jd_isotp_request(&c, PID05, 2, 0);
    TEST_ASSERT_EQUAL(JD_ERR_BUSY, jd_isotp_request(&c, PID05, 2, 1));
    TEST_ASSERT_EQUAL(1, txl.count);
}

static void test_tx_refused(void)
{
    txl.refuse = true;
    TEST_ASSERT_EQUAL(JD_ERR_TX_REFUSED, jd_isotp_request(&c, PID05, 2, 0));
    TEST_ASSERT_EQUAL(JD_REQ_FAILED, c.state);
    TEST_ASSERT_EQUAL(JD_ERR_TX_REFUSED, c.err);
}

static void test_frames_when_idle_ignored(void)
{
    rx(0, "03 41 05 6B 00 00 00 00");
    TEST_ASSERT_EQUAL(JD_REQ_IDLE, c.state);
    TEST_ASSERT_EQUAL(0, txl.count);
}

static void test_reply_to_other_service_ignored(void)
{
    jd_isotp_request(&c, PID05, 2, 0);
    rx(5, "03 7F 22 78 00 00 00 00"); // pending for a different service: not ours
    rx(6, "03 62 F1 90 00 00 00 00"); // unrelated positive reply
    TEST_ASSERT_EQUAL(JD_REQ_BUSY, c.state);
}

static void test_next_request_after_done(void)
{
    jd_isotp_request(&c, PID05, 2, 0);
    rx(10, "03 41 05 6B 00 00 00 00");
    TEST_ASSERT_EQUAL(JD_OK, jd_isotp_request(&c, PID78, 2, 20));
    TEST_ASSERT_EQUAL(JD_REQ_BUSY, c.state);
}

// Integration: everything the client sends passes the real allowlist.
static bool guarded_send(void *ctx, const jd_frame_t *f)
{
    TEST_ASSERT_TRUE_MESSAGE(jd_guard_check(f), "client sent a frame the guard refuses");
    return tx_log_send(ctx, f);
}

static void test_client_frames_pass_guard(void)
{
    jd_isotp_init(&c, 0x7E0, 0x7E8, guarded_send, &txl);
    jd_isotp_request(&c, PID78, 2, 0);
    rx(10, "10 0B 41 78 0F 08 4F 07");
    rx(11, "21 08 06 F2 06 2B 00 00");
    TEST_ASSERT_EQUAL(JD_REQ_DONE, c.state);
    TEST_ASSERT_EQUAL(2, txl.count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_request_is_padded_single_frame);
    RUN_TEST(test_single_frame_reply);
    RUN_TEST(test_other_ids_ignored);
    RUN_TEST(test_multi_frame_pid78_from_capture);
    RUN_TEST(test_long_multi_frame_message);
    RUN_TEST(test_wrong_sequence_fails);
    RUN_TEST(test_too_long_reply_fails);
    RUN_TEST(test_timeout_without_reply);
    RUN_TEST(test_timeout_between_consecutive_frames);
    RUN_TEST(test_time_wraparound);
    RUN_TEST(test_response_pending_extends_wait);
    RUN_TEST(test_negative_response);
    RUN_TEST(test_busy_and_bad_requests);
    RUN_TEST(test_tx_refused);
    RUN_TEST(test_frames_when_idle_ignored);
    RUN_TEST(test_reply_to_other_service_ignored);
    RUN_TEST(test_next_request_after_done);
    RUN_TEST(test_client_frames_pass_guard);
    return UNITY_END();
}
