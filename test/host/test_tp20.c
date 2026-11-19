// VW TP 2.0 channel to the engine. Byte sequences are from the 2026-11-18 captures;
// the error cases port macvagdiag's Tp20ChannelTest.
#include "jd/guard.h"
#include "jd/tp20.h"
#include "test_util.h"

static tx_log_t  txl;
static jd_tp20_t t;
static uint32_t  now;

void setUp(void)
{
    memset(&txl, 0, sizeof txl);
    jd_tp20_init(&t, tx_log_send, &txl);
    now = 0;
}
void tearDown(void) {}

static void eng(const char *hex) { jd_frame_t f = F(0x300, hex); jd_tp20_on_frame(&t, &f, now); }
static void tick(uint32_t ms)    { now += ms; jd_tp20_tick(&t, now); }
static void at(uint32_t ms)      { now = ms; jd_tp20_tick(&t, now); }

// Setup + timing exchange, exactly as VCDS did it. Leaves now = 2, 2 frames sent.
static void open_channel(void)
{
    TEST_ASSERT_EQUAL(JD_OK, jd_tp20_open(&t, now));
    now = 1;
    jd_frame_t setup = F(0x201, "00 D0 00 03 40 07 01");
    jd_tp20_on_frame(&t, &setup, now);
    now = 2;
    eng("A1 0F 8A FF 4A FF");
    TEST_ASSERT_TRUE(jd_tp20_is_open(&t));
    TEST_ASSERT_EQUAL(2, txl.count);
}

static const uint8_t SESSION[] = { 0x10, 0x89 };
static const uint8_t GROUP99[] = { 0x21, 0x63 };

// Sends `req` past the request gap and has the engine ACK it.
static void request_acked(const uint8_t *req, uint8_t len, const char *expect_packet, const char *ack)
{
    int before = txl.count;
    TEST_ASSERT_EQUAL(JD_OK, jd_tp20_request(&t, req, len, now));
    tick(JD_TP20_REQUEST_GAP);
    TEST_ASSERT_EQUAL(before + 1, txl.count);
    ASSERT_SENT(&txl, before, 0x740, expect_packet);
    eng(ack);
}

// ---------------------------------------------------------------- setup

static void test_setup_and_timing_exchange(void)
{
    open_channel();
    ASSERT_SENT(&txl, 0, 0x200, "01 C0 00 10 00 03 01");
    ASSERT_SENT(&txl, 1, 0x740, "A0 0F 8A FF 32 FF");
    TEST_ASSERT_EQUAL(100, t.ack_timeout); // engine T1 8A = 10 x 10 ms
}

static void test_decode_time_and_id(void)
{
    TEST_ASSERT_EQUAL(100000, jd_tp20_decode_time_us(0x8A)); // 10 x 10 ms
    TEST_ASSERT_EQUAL(5000, jd_tp20_decode_time_us(0x32));   // 50 x 0.1 ms
    TEST_ASSERT_EQUAL(10000, jd_tp20_decode_time_us(0x4A));  // 10 x 1 ms
    TEST_ASSERT_EQUAL(500000, jd_tp20_decode_time_us(0xC5)); // 5 x 100 ms
    TEST_ASSERT_EQUAL(0x740, jd_tp20_decode_id(0x40, 0x07));
    TEST_ASSERT_EQUAL(0x300, jd_tp20_decode_id(0x00, 0x03));
    TEST_ASSERT_EQUAL(-1, jd_tp20_decode_id(0x00, 0x10));
}

static void test_setup_timeout(void)
{
    jd_tp20_open(&t, 0);
    at(JD_TP20_SETUP_TIMEOUT - 1);
    TEST_ASSERT_EQUAL(JD_TP20_SETUP, t.state);
    at(JD_TP20_SETUP_TIMEOUT);
    TEST_ASSERT_EQUAL(JD_TP20_CLOSED, t.state);
    TEST_ASSERT_EQUAL(JD_ERR_TIMEOUT, t.err);
    TEST_ASSERT_EQUAL(1, txl.count); // nothing else was sent
}

static void test_setup_reply_for_someone_else_ignored(void)
{
    jd_tp20_open(&t, 0);
    jd_frame_t other = F(0x201, "01 D0 00 03 40 07 01"); // addressed to tester 01
    jd_tp20_on_frame(&t, &other, 1);
    jd_frame_t gateway = F(0x21F, "00 D0 00 03 40 07 01"); // another module's reply ID
    jd_tp20_on_frame(&t, &gateway, 1);
    TEST_ASSERT_EQUAL(JD_TP20_SETUP, t.state);
    TEST_ASSERT_EQUAL(1, txl.count);
}

static void test_setup_refused(void)
{
    jd_tp20_open(&t, 0);
    jd_frame_t refused = F(0x201, "00 D6");
    jd_tp20_on_frame(&t, &refused, 1);
    TEST_ASSERT_EQUAL(JD_TP20_CLOSED, t.state);
    TEST_ASSERT_EQUAL(JD_ERR_NEGATIVE, t.err);
    TEST_ASSERT_EQUAL(1, txl.count);
}

static void test_negotiated_id_not_ours_is_never_used(void)
{
    jd_tp20_open(&t, 0);
    jd_frame_t odd = F(0x201, "00 D0 00 03 60 07 01"); // engine says "send on 760"
    jd_tp20_on_frame(&t, &odd, 1);
    TEST_ASSERT_EQUAL(JD_TP20_CLOSED, t.state);
    TEST_ASSERT_EQUAL(JD_ERR_PROTOCOL, t.err);
    TEST_ASSERT_EQUAL(1, txl.count); // nothing sent on 760 (or anywhere)
}

static void test_setup_invalid_id(void)
{
    jd_tp20_open(&t, 0);
    jd_frame_t bad = F(0x201, "00 D0 00 03 00 10 01");
    jd_tp20_on_frame(&t, &bad, 1);
    TEST_ASSERT_EQUAL(JD_ERR_PROTOCOL, t.err);
    TEST_ASSERT_EQUAL(1, txl.count);
}

static void test_timing_reply_timeout_sends_disconnect(void)
{
    jd_tp20_open(&t, 0);
    jd_frame_t setup = F(0x201, "00 D0 00 03 40 07 01");
    jd_tp20_on_frame(&t, &setup, 1);
    at(1 + JD_TP20_CONTROL_TIMEOUT);
    TEST_ASSERT_EQUAL(JD_TP20_CLOSED, t.state);
    TEST_ASSERT_EQUAL(JD_ERR_TIMEOUT, t.err);
    ASSERT_SENT(&txl, 2, 0x740, "A8");
}

static void test_tx_refused_at_setup(void)
{
    txl.refuse = true;
    TEST_ASSERT_EQUAL(JD_ERR_TX_REFUSED, jd_tp20_open(&t, 0));
    TEST_ASSERT_EQUAL(JD_TP20_CLOSED, t.state);
}

// ---------------------------------------------------------------- requests

static void test_session_request_single_packet(void)
{
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "B1");
    TEST_ASSERT_EQUAL(JD_REQ_BUSY, t.req_state);
    eng("10 00 02 50 89");
    ASSERT_SENT(&txl, 3, 0x740, "B1");
    TEST_ASSERT_EQUAL(JD_REQ_DONE, t.req_state);
    size_t n;
    const uint8_t *r = jd_tp20_response(&t, &n);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(((uint8_t[]){ 0x50, 0x89 }), r, 2);
}

static void test_group_099_multi_packet_reply(void)
{
    open_channel();
    request_acked(GROUP99, 2, "10 00 02 21 63", "B1");
    eng("20 00 1A 61 63 01 73 24");
    eng("21 05 3C 7F 05 3C 79 05");
    eng("22 3C 78 25 00 00 25 00");
    TEST_ASSERT_EQUAL(3, txl.count); // no ACK yet: those packets don't ask for one
    eng("13 00 30 00 00 30 00 00");
    ASSERT_SENT(&txl, 3, 0x740, "B4");
    TEST_ASSERT_EQUAL(JD_REQ_DONE, t.req_state);
    size_t n;
    const uint8_t *r = jd_tp20_response(&t, &n);
    TEST_ASSERT_EQUAL(26, n);
    TEST_ASSERT_EQUAL_HEX8(0x61, r[0]);
    TEST_ASSERT_EQUAL_HEX8(0x7F, r[7]);   // EGT b byte
    TEST_ASSERT_EQUAL_HEX8(0x00, r[25]);
}

static void test_engine_asks_ack_mid_message(void)
{
    open_channel();
    request_acked(GROUP99, 2, "10 00 02 21 63", "B1");
    eng("00 00 08 61 63 01 73 24"); // op 0: more, ACK wanted
    ASSERT_SENT(&txl, 3, 0x740, "B1");
    eng("11 05 3C 7F");
    ASSERT_SENT(&txl, 4, 0x740, "B2");
    TEST_ASSERT_EQUAL(JD_REQ_DONE, t.req_state);
}

static void test_request_waits_for_gap_after_our_ack(void)
{
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "B1");
    eng("10 00 02 50 89");           // we ACK B1 at `now`
    uint32_t acked_at = now;
    TEST_ASSERT_EQUAL(4, txl.count);
    jd_tp20_request(&t, GROUP99, 2, now);
    at(acked_at + JD_TP20_REQUEST_GAP - 1);
    TEST_ASSERT_EQUAL(4, txl.count); // too soon
    at(acked_at + JD_TP20_REQUEST_GAP);
    TEST_ASSERT_EQUAL(5, txl.count);
    ASSERT_SENT(&txl, 4, 0x740, "11 00 02 21 63"); // sequence moved on
}

static void test_sequence_numbers_wrap(void)
{
    open_channel();
    for (int i = 0; i < 17; i++) {
        char pkt[32], ack[8], reply[32], our_ack[8];
        snprintf(pkt, sizeof pkt, "1%X 00 02 10 89", i & 0xF);
        snprintf(ack, sizeof ack, "B%X", (i + 1) & 0xF);
        snprintf(reply, sizeof reply, "1%X 00 02 50 89", i & 0xF);
        snprintf(our_ack, sizeof our_ack, "B%X", (i + 1) & 0xF);
        request_acked(SESSION, 2, pkt, ack);
        eng(reply);
        ASSERT_SENT(&txl, txl.count - 1, 0x740, our_ack);
        TEST_ASSERT_EQUAL(JD_REQ_DONE, t.req_state);
    }
}

static void test_not_ready_then_ready(void)
{
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "91"); // taken, but not ready
    tick(50);
    eng("B1");
    eng("10 00 02 50 89");
    TEST_ASSERT_EQUAL(JD_REQ_DONE, t.req_state);
}

static void test_not_ready_forever(void)
{
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "91");
    for (int i = 0; i < JD_TP20_MAX_NOT_READY; i++) { tick(10); eng("91"); }
    TEST_ASSERT_EQUAL(JD_REQ_FAILED, t.req_state);
    TEST_ASSERT_EQUAL(JD_ERR_TIMEOUT, t.req_err);
    TEST_ASSERT_EQUAL(JD_TP20_CLOSED, t.state);
    ASSERT_SENT(&txl, txl.count - 1, 0x740, "A8");
}

static void test_ack_timeout_retries_capped(void)
{
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "");
    for (int i = 0; i < JD_TP20_MAX_RETRIES; i++) {
        int before = txl.count;
        tick(t.ack_timeout);
        tick(JD_TP20_REQUEST_GAP);
        TEST_ASSERT_EQUAL(before + 1, txl.count);
        ASSERT_SENT(&txl, before, 0x740, "10 00 02 10 89"); // same packet, same sequence
    }
    tick(t.ack_timeout);
    TEST_ASSERT_EQUAL(JD_REQ_FAILED, t.req_state);
    TEST_ASSERT_EQUAL(JD_ERR_TIMEOUT, t.req_err);
    ASSERT_SENT(&txl, txl.count - 1, 0x740, "A8");
}

static void test_ack_after_retry(void)
{
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "");
    tick(t.ack_timeout);
    tick(JD_TP20_REQUEST_GAP);
    eng("B1");
    eng("10 00 02 50 89");
    TEST_ASSERT_EQUAL(JD_REQ_DONE, t.req_state);
}

static void test_wrong_ack_sequence(void)
{
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "B5");
    TEST_ASSERT_EQUAL(JD_REQ_FAILED, t.req_state);
    TEST_ASSERT_EQUAL(JD_ERR_PROTOCOL, t.req_err);
    ASSERT_SENT(&txl, txl.count - 1, 0x740, "A8");
}

static void test_not_taken_then_resent(void)
{
    // 2026-11-17 on the car: "90" names our own packet 0 (not taken, not ready); after B0 we resend.
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "90");
    int before = txl.count;
    tick(30);
    TEST_ASSERT_EQUAL(before, txl.count);  // wait for the ready ACK
    eng("B0");
    tick(JD_TP20_REQUEST_GAP);
    ASSERT_SENT(&txl, before, 0x740, "10 00 02 10 89");
    eng("B1");
    eng("10 00 02 50 89");
    TEST_ASSERT_EQUAL(JD_REQ_DONE, t.req_state);
}

static void test_not_taken_ready_resends_immediately(void)
{
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "B0"); // ready, but still expects packet 0
    int before = txl.count;
    tick(JD_TP20_REQUEST_GAP);
    ASSERT_SENT(&txl, before, 0x740, "10 00 02 10 89");
}

static void test_repeated_packet_acked_again_and_ignored(void)
{
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "B1");
    eng("10 00 02 50 89");
    int before = txl.count;
    tick(150);
    eng("10 00 02 50 89"); // the engine missed our ACK and repeats
    TEST_ASSERT_EQUAL(before + 1, txl.count);
    ASSERT_SENT(&txl, before, 0x740, "B1");
    TEST_ASSERT_TRUE(jd_tp20_is_open(&t));
}

static void test_out_of_sequence_different_bytes_fails(void)
{
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "B1");
    eng("10 00 02 50 89");
    jd_tp20_clear(&t);
    eng("10 00 02 51 89"); // seq 0 again, but different bytes
    TEST_ASSERT_EQUAL(JD_TP20_CLOSED, t.state);
    TEST_ASSERT_EQUAL(JD_ERR_PROTOCOL, t.err);
}

static void test_wrong_receive_sequence(void)
{
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "B1");
    eng("13 00 02 50 89");
    TEST_ASSERT_EQUAL(JD_REQ_FAILED, t.req_state);
    TEST_ASSERT_EQUAL(JD_ERR_PROTOCOL, t.req_err);
    TEST_ASSERT_EQUAL(JD_TP20_CLOSED, t.state);
}

static void test_length_mismatch(void)
{
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "B1");
    eng("10 00 05 50 89"); // last packet, but length says 5
    TEST_ASSERT_EQUAL(JD_ERR_PROTOCOL, t.req_err);
}

static void test_too_long_message(void)
{
    open_channel();
    request_acked(GROUP99, 2, "10 00 02 21 63", "B1");
    eng("20 0F FF 61 63 01 73 24");
    TEST_ASSERT_EQUAL(JD_ERR_PROTOCOL, t.req_err);
}

static void test_other_ids_ignored(void)
{
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "B1");
    jd_frame_t other = F(0x301, "10 00 02 50 89");
    jd_tp20_on_frame(&t, &other, now);
    TEST_ASSERT_EQUAL(JD_REQ_BUSY, t.req_state);
}

static void test_response_timeout(void)
{
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "B1");
    tick(JD_TP20_RESPONSE_TIMEOUT - 1);
    TEST_ASSERT_EQUAL(JD_REQ_BUSY, t.req_state);
    tick(1);
    TEST_ASSERT_EQUAL(JD_REQ_FAILED, t.req_state);
    TEST_ASSERT_EQUAL(JD_ERR_TIMEOUT, t.req_err);
}

static void test_response_pending_with_length_flag(void)
{
    // From the capture: 7F xx 78 arrives with length 80 03.
    open_channel();
    request_acked(GROUP99, 2, "10 00 02 21 63", "B1");
    eng("10 80 03 7F 21 78");
    ASSERT_SENT(&txl, 3, 0x740, "B1");
    TEST_ASSERT_EQUAL(JD_REQ_BUSY, t.req_state);
    tick(JD_TP20_RESPONSE_TIMEOUT + 500); // pending extends the wait
    TEST_ASSERT_EQUAL(JD_REQ_BUSY, t.req_state);
    eng("11 00 02 61 63");
    TEST_ASSERT_EQUAL(JD_REQ_DONE, t.req_state);
}

static void test_negative_reply_keeps_channel(void)
{
    open_channel();
    request_acked(GROUP99, 2, "10 00 02 21 63", "B1");
    eng("10 00 03 7F 21 31");
    TEST_ASSERT_EQUAL(JD_REQ_FAILED, t.req_state);
    TEST_ASSERT_EQUAL(JD_ERR_NEGATIVE, t.req_err);
    TEST_ASSERT_EQUAL_HEX8(0x31, t.nrc);
    TEST_ASSERT_TRUE(jd_tp20_is_open(&t));
}

static void test_stale_reply_without_request_dropped(void)
{
    open_channel();
    eng("10 00 02 50 89");
    ASSERT_SENT(&txl, 2, 0x740, "B1"); // still ACKed (the engine asked)
    TEST_ASSERT_EQUAL(JD_REQ_IDLE, t.req_state);
    TEST_ASSERT_TRUE(jd_tp20_is_open(&t));
}

static void test_bad_requests(void)
{
    uint8_t six[6] = { 0 };
    TEST_ASSERT_EQUAL(JD_ERR_CLOSED, jd_tp20_request(&t, SESSION, 2, 0));
    open_channel();
    TEST_ASSERT_EQUAL(JD_ERR_ARG, jd_tp20_request(&t, six, 6, now));
    TEST_ASSERT_EQUAL(JD_ERR_ARG, jd_tp20_request(&t, six, 0, now));
    TEST_ASSERT_EQUAL(JD_OK, jd_tp20_request(&t, SESSION, 2, now));
    TEST_ASSERT_EQUAL(JD_ERR_BUSY, jd_tp20_request(&t, SESSION, 2, now));
}

// ---------------------------------------------------------------- channel upkeep

static void test_keepalive_matches_vcds_rhythm(void)
{
    // VCDS sent A3 every ~0.55 s (min 0.49 s), also while polling groups (2026-11-18).
    TEST_ASSERT_TRUE(JD_TP20_KEEPALIVE_MS <= 550);
    TEST_ASSERT_TRUE(JD_TP20_KEEPALIVE_MS >= 450);
}

static void test_keepalive_when_idle(void)
{
    open_channel();
    tick(JD_TP20_KEEPALIVE_MS - 1);
    TEST_ASSERT_EQUAL(2, txl.count);
    tick(1);
    ASSERT_SENT(&txl, 2, 0x740, "A3");
    eng("A1 0F 8A FF 4A FF");
    tick(JD_TP20_CONTROL_TIMEOUT + 10);
    TEST_ASSERT_TRUE(jd_tp20_is_open(&t));
}

static void test_keepalive_timeout_closes(void)
{
    open_channel();
    tick(JD_TP20_KEEPALIVE_MS);
    tick(JD_TP20_CONTROL_TIMEOUT);
    TEST_ASSERT_EQUAL(JD_TP20_CLOSED, t.state);
    TEST_ASSERT_EQUAL(JD_ERR_TIMEOUT, t.err);
    ASSERT_SENT(&txl, txl.count - 1, 0x740, "A8");
}

static void test_request_waits_for_keepalive_reply(void)
{
    open_channel();
    tick(JD_TP20_KEEPALIVE_MS);           // A3 out
    jd_tp20_request(&t, SESSION, 2, now);
    tick(JD_TP20_REQUEST_GAP + 5);
    TEST_ASSERT_EQUAL(3, txl.count);      // held until A1
    eng("A1 0F 8A FF 4A FF");
    tick(JD_TP20_REQUEST_GAP);
    ASSERT_SENT(&txl, 3, 0x740, "10 00 02 10 89");
}

static void test_engine_channel_test_answered(void)
{
    open_channel();
    eng("A3");
    ASSERT_SENT(&txl, 2, 0x740, "A1 0F 8A FF 32 FF");
}

static void test_engine_disconnects(void)
{
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "B1");
    eng("A8");
    TEST_ASSERT_EQUAL(JD_TP20_CLOSED, t.state);
    TEST_ASSERT_EQUAL(JD_ERR_CLOSED, t.err);
    TEST_ASSERT_EQUAL(JD_REQ_FAILED, t.req_state);
    TEST_ASSERT_EQUAL(JD_ERR_CLOSED, t.req_err);
}

static void test_close_sends_a8(void)
{
    open_channel();
    jd_tp20_close(&t, now);
    ASSERT_SENT(&txl, 2, 0x740, "A8");
    TEST_ASSERT_EQUAL(JD_TP20_CLOSED, t.state);
    jd_tp20_close(&t, now);
    TEST_ASSERT_EQUAL(3, txl.count); // only once
}

static void test_reopen_after_close(void)
{
    open_channel();
    jd_tp20_close(&t, now);
    memset(&txl, 0, sizeof txl);
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "B1"); // sequences restart at 0
}

// ---------------------------------------------------------------- integration with the guard

static bool guarded_send(void *ctx, const jd_frame_t *f)
{
    TEST_ASSERT_TRUE_MESSAGE(jd_guard_check(f), "TP 2.0 sent a frame the guard refuses");
    return tx_log_send(ctx, f);
}

static void test_all_frames_pass_guard(void)
{
    jd_tp20_init(&t, guarded_send, &txl);
    open_channel();
    request_acked(SESSION, 2, "10 00 02 10 89", "B1");
    eng("10 00 02 50 89");
    request_acked(GROUP99, 2, "11 00 02 21 63", "B2");
    eng("21 00 1A 61 63 01 73 24");
    eng("22 05 3C 7F 05 3C 79 05");
    eng("23 3C 78 25 00 00 25 00");
    eng("14 00 30 00 00 30 00 00");
    TEST_ASSERT_EQUAL(JD_REQ_DONE, t.req_state);
    eng("A3");
    tick(JD_TP20_KEEPALIVE_MS);
    jd_tp20_close(&t, now);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_setup_and_timing_exchange);
    RUN_TEST(test_decode_time_and_id);
    RUN_TEST(test_setup_timeout);
    RUN_TEST(test_setup_reply_for_someone_else_ignored);
    RUN_TEST(test_setup_refused);
    RUN_TEST(test_negotiated_id_not_ours_is_never_used);
    RUN_TEST(test_setup_invalid_id);
    RUN_TEST(test_timing_reply_timeout_sends_disconnect);
    RUN_TEST(test_tx_refused_at_setup);
    RUN_TEST(test_session_request_single_packet);
    RUN_TEST(test_group_099_multi_packet_reply);
    RUN_TEST(test_engine_asks_ack_mid_message);
    RUN_TEST(test_request_waits_for_gap_after_our_ack);
    RUN_TEST(test_sequence_numbers_wrap);
    RUN_TEST(test_not_ready_then_ready);
    RUN_TEST(test_not_ready_forever);
    RUN_TEST(test_ack_timeout_retries_capped);
    RUN_TEST(test_ack_after_retry);
    RUN_TEST(test_wrong_ack_sequence);
    RUN_TEST(test_not_taken_then_resent);
    RUN_TEST(test_not_taken_ready_resends_immediately);
    RUN_TEST(test_repeated_packet_acked_again_and_ignored);
    RUN_TEST(test_out_of_sequence_different_bytes_fails);
    RUN_TEST(test_wrong_receive_sequence);
    RUN_TEST(test_length_mismatch);
    RUN_TEST(test_too_long_message);
    RUN_TEST(test_other_ids_ignored);
    RUN_TEST(test_response_timeout);
    RUN_TEST(test_response_pending_with_length_flag);
    RUN_TEST(test_negative_reply_keeps_channel);
    RUN_TEST(test_stale_reply_without_request_dropped);
    RUN_TEST(test_bad_requests);
    RUN_TEST(test_keepalive_matches_vcds_rhythm);
    RUN_TEST(test_keepalive_when_idle);
    RUN_TEST(test_keepalive_timeout_closes);
    RUN_TEST(test_request_waits_for_keepalive_reply);
    RUN_TEST(test_engine_channel_test_answered);
    RUN_TEST(test_engine_disconnects);
    RUN_TEST(test_close_sends_a8);
    RUN_TEST(test_reopen_after_close);
    RUN_TEST(test_all_frames_pass_guard);
    return UNITY_END();
}
