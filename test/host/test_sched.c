// Poll scheduler: rate limits, back-off, quiet mode.
#include "jd/sched.h"
#include "test_util.h"

static jd_sched_t s;

void setUp(void) { jd_sched_init(&s); }
void tearDown(void) {}

static void test_items_polled_when_due(void)
{
    int egt = jd_sched_add(&s, 1000, 0, 0);
    TEST_ASSERT_EQUAL(egt, jd_sched_next(&s, 0));
    TEST_ASSERT_EQUAL(-1, jd_sched_next(&s, 0)); // in flight
    jd_sched_done(&s, true, 50);
    TEST_ASSERT_EQUAL(-1, jd_sched_next(&s, 999));
    TEST_ASSERT_EQUAL(egt, jd_sched_next(&s, 1000)); // period counts from when it was sent
}

static void test_min_gap_between_any_requests(void)
{
    int a = jd_sched_add(&s, 1000, 0, 0);
    int b = jd_sched_add(&s, 1000, 0, 0);
    TEST_ASSERT_EQUAL(a, jd_sched_next(&s, 0));
    jd_sched_done(&s, true, 10);
    TEST_ASSERT_EQUAL(-1, jd_sched_next(&s, JD_SCHED_MIN_GAP_MS - 1));
    TEST_ASSERT_EQUAL(b, jd_sched_next(&s, JD_SCHED_MIN_GAP_MS));
}

static void test_rate_never_exceeds_limit(void)
{
    // Many fast items, all answering instantly: requests are still >= MIN_GAP apart.
    for (int i = 0; i < JD_SCHED_MAX_ITEMS; i++) jd_sched_add(&s, 100, 0, 0);
    int requests = 0;
    for (uint32_t now = 0; now < 60000; now += 5) {
        if (jd_sched_next(&s, now) >= 0) { requests++; jd_sched_done(&s, true, now); }
    }
    TEST_ASSERT_TRUE(requests <= 60000 / JD_SCHED_MIN_GAP_MS + 1);
    TEST_ASSERT_TRUE(requests >= 60000 / JD_SCHED_MIN_GAP_MS - 1);
}

static void test_most_overdue_first(void)
{
    int slow = jd_sched_add(&s, 5000, 0, 0);
    int fast = jd_sched_add(&s, 1000, 0, 0);
    TEST_ASSERT_EQUAL(slow, jd_sched_next(&s, 0));   // tie: lowest index
    jd_sched_done(&s, true, 0);
    TEST_ASSERT_EQUAL(fast, jd_sched_next(&s, 300));
    jd_sched_done(&s, true, 300);
    // both round-robin fairly: no starvation of the slow item
    int seen_slow = 0;
    for (uint32_t now = 600; now < 20000; now += 10) {
        int i = jd_sched_next(&s, now);
        if (i == slow) seen_slow++;
        if (i >= 0) jd_sched_done(&s, true, now);
    }
    TEST_ASSERT_TRUE(seen_slow >= 3);
}

static void test_backoff_doubles_and_caps(void)
{
    int a = jd_sched_add(&s, 1000, 0, 0);
    uint32_t now = 0;
    uint32_t expected_wait[] = { 2000, 4000, 8000, 16000, 30000, 30000 };
    for (int i = 0; i < 4; i++) { // stay under the quiet threshold
        TEST_ASSERT_EQUAL(a, jd_sched_next(&s, now));
        jd_sched_done(&s, false, now);
        TEST_ASSERT_EQUAL(-1, jd_sched_next(&s, now + expected_wait[i] - 1));
        now += expected_wait[i];
    }
    TEST_ASSERT_EQUAL(a, jd_sched_next(&s, now));
    jd_sched_done(&s, true, now);
    TEST_ASSERT_EQUAL(0, s.items[a].fails);
    TEST_ASSERT_EQUAL(a, jd_sched_next(&s, now + 1000)); // back to normal period
}

static void test_quiet_mode_after_failures(void)
{
    int a = jd_sched_add(&s, 1000, 0, 0);
    int b = jd_sched_add(&s, 1000, 0, 0);
    (void)a; (void)b;
    uint32_t now = 0;
    int fails = 0;
    while (fails < JD_SCHED_QUIET_AFTER) {
        int i = jd_sched_next(&s, now);
        if (i >= 0) { jd_sched_done(&s, false, now); fails++; }
        now += 10;
    }
    TEST_ASSERT_TRUE(s.quiet);
    uint32_t quiet_at = now - 10;
    // Nothing until the probe interval, whatever the items' own back-off says.
    for (; now < quiet_at + JD_SCHED_QUIET_PROBE_MS; now += 10) TEST_ASSERT_EQUAL(-1, jd_sched_next(&s, now));
    int probe = jd_sched_next(&s, now);
    TEST_ASSERT_TRUE(probe >= 0);
    jd_sched_done(&s, false, now);
    TEST_ASSERT_EQUAL(-1, jd_sched_next(&s, now + JD_SCHED_QUIET_PROBE_MS - 1));
    // A probe that succeeds ends quiet mode and everything resumes.
    TEST_ASSERT_TRUE(jd_sched_next(&s, now + JD_SCHED_QUIET_PROBE_MS) >= 0);
    jd_sched_done(&s, true, now + JD_SCHED_QUIET_PROBE_MS);
    TEST_ASSERT_FALSE(s.quiet);
    TEST_ASSERT_EQUAL(0, s.fail_streak);
}

static void test_quiet_exit_resets_all_backoff(void)
{
    // ECU wakes up: every item polls again straight away, not after its own back-off.
    int a = jd_sched_add(&s, 1000, 0, 0);
    int b = jd_sched_add(&s, 1000, 0, 0);
    uint32_t now = 0;
    for (int fails = 0; fails < 10; now += 10) {
        if (jd_sched_next(&s, now) >= 0) { jd_sched_done(&s, false, now); fails++; }
    }
    TEST_ASSERT_TRUE(s.quiet);
    now += JD_SCHED_QUIET_PROBE_MS;
    int probe = jd_sched_next(&s, now);
    jd_sched_done(&s, true, now);
    int other = probe == a ? b : a;
    TEST_ASSERT_EQUAL(other, jd_sched_next(&s, now + JD_SCHED_MIN_GAP_MS));
    TEST_ASSERT_EQUAL(0, s.items[other].fails);
}

static void test_success_resets_streak(void)
{
    jd_sched_add(&s, 1000, 0, 0);
    jd_sched_add(&s, 1000, 0, 0);
    uint32_t now = 0;
    for (int i = 0; i < 40; i++, now += 400) {
        if (jd_sched_next(&s, now) >= 0) jd_sched_done(&s, i % 2 == 0, now); // alternate
    }
    TEST_ASSERT_FALSE(s.quiet);
}

static void test_capacity(void)
{
    for (int i = 0; i < JD_SCHED_MAX_ITEMS; i++) TEST_ASSERT_EQUAL(i, jd_sched_add(&s, 1000, 0, 0));
    TEST_ASSERT_EQUAL(-1, jd_sched_add(&s, 1000, 0, 0));
}

static void test_done_without_request_is_harmless(void)
{
    jd_sched_add(&s, 1000, 0, 0);
    jd_sched_done(&s, false, 0);
    TEST_ASSERT_EQUAL(0, s.fail_streak);
    TEST_ASSERT_EQUAL(0, jd_sched_next(&s, 0));
}

static void test_time_wrap(void)
{
    uint32_t t0 = 0xFFFFFF00u;
    int a = jd_sched_add(&s, 1000, t0, 0);
    TEST_ASSERT_EQUAL(a, jd_sched_next(&s, t0));
    jd_sched_done(&s, true, t0);
    TEST_ASSERT_EQUAL(-1, jd_sched_next(&s, t0 + 999));
    TEST_ASSERT_EQUAL(a, jd_sched_next(&s, t0 + 1000));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_items_polled_when_due);
    RUN_TEST(test_min_gap_between_any_requests);
    RUN_TEST(test_rate_never_exceeds_limit);
    RUN_TEST(test_most_overdue_first);
    RUN_TEST(test_backoff_doubles_and_caps);
    RUN_TEST(test_quiet_mode_after_failures);
    RUN_TEST(test_quiet_exit_resets_all_backoff);
    RUN_TEST(test_success_resets_streak);
    RUN_TEST(test_capacity);
    RUN_TEST(test_done_without_request_is_harmless);
    RUN_TEST(test_time_wrap);
    return UNITY_END();
}
