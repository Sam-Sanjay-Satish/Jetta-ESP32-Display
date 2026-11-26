// Whole diagnostic stack against simulated engine ECUs that answer like the real one
// in the 2026-11-18 captures. Every frame goes through the real gate and allowlist.
#include "jd/diag.h"
#include "jd/guard.h"
#include "test_util.h"

// ---------------------------------------------------------------- fake bus + engines

typedef enum { ECU_OBD, ECU_TP20, ECU_SILENT } ecu_kind_t;

static struct {
    ecu_kind_t kind;
    jd_frame_t sent[4096];
    int        sent_count;
    jd_frame_t pending[32];   // replies the engine will put on the bus next tick
    int        pending_count;
    uint8_t    eng_seq;       // TP 2.0: engine's own sequence counter
    bool       pid78_cf_due;
    int        group_reads[256]; // 21 xx requests the fake engine received, by group
    int        setups, keepalives;
    bool       drop_channel_once;
    bool       refuse_ident;
} bus;

static jd_txgate_t gate;
static jd_diag_t   d;
static uint32_t    now;

static void reply(uint16_t id, const char *hex)
{
    TEST_ASSERT_TRUE(bus.pending_count < 32);
    bus.pending[bus.pending_count++] = F(id, hex);
}

static void tp_data(const char *fmt_after_seq, uint8_t op)
{
    char hex[40];
    snprintf(hex, sizeof hex, "%X%X %s", op, bus.eng_seq, fmt_after_seq);
    bus.eng_seq = (bus.eng_seq + 1) & 0x0F;
    reply(0x300, hex);
}

static void fake_obd(const jd_frame_t *f)
{
    if (f->id != 0x7E0) return;
    if (f->data[0] == 0x30) { TEST_ASSERT_EQUAL_HEX8(0x01, f->data[2]); if (bus.pid78_cf_due) { reply(0x7E8, "21 08 06 F2 06 2B 00 00"); bus.pid78_cf_due = false; } return; }
    switch (f->data[2]) {
    case 0x05: reply(0x7E8, "03 41 05 6B 00 00 00 00"); break;
    case 0x0C: reply(0x7E8, "04 41 0C 0C F0 00 00 00"); break;
    case 0x78: reply(0x7E8, "10 0B 41 78 0F 08 4F 07"); bus.pid78_cf_due = true; break;
    }
}

static void fake_tp20(const jd_frame_t *f)
{
    if (f->id == 0x200) { bus.setups++; bus.eng_seq = 0; reply(0x201, "00 D0 00 03 40 07 01"); return; }
    if (f->id != 0x740 || f->len == 0) return;
    uint8_t b0 = f->data[0];
    if (b0 == 0xA0) { reply(0x300, "A1 0F 8A FF 4A FF"); return; }
    if (b0 == 0xA3) { bus.keepalives++; reply(0x300, "A1 0F 8A FF 4A FF"); return; }
    if (b0 == 0xA8) { reply(0x300, "A8"); return; }
    if ((b0 >> 4) == 0xB) return; // tester's ACK
    if ((b0 >> 4) == 0x1 && f->len == 5) {
        char ack[4];
        snprintf(ack, sizeof ack, "B%X", (b0 + 1) & 0x0F);
        reply(0x300, ack);
        if (bus.drop_channel_once && f->data[3] == 0x21) { bus.drop_channel_once = false; reply(0x300, "A8"); return; }
        if (f->data[3] == 0x10) { tp_data("00 02 50 89", 0x1); return; }
        if (f->data[3] == 0x1A && bus.refuse_ident) { tp_data("00 03 7F 1A 11", 0x1); return; }
        if (f->data[3] == 0x1A) { tp_data("00 08 5A 9B 30 33 4C", 0x2); tp_data("39 39 37", 0x1); return; }
        if (f->data[3] == 0x21) bus.group_reads[f->data[4]]++;
        if (f->data[3] == 0x21 && f->data[4] == 0x0B) { // group 011: RPM, boost spec 2300, actual 2000
            tp_data("00 1A 61 0B 01 73 24", 0x2);
            tp_data("60 FA 5C 60 FA 50 21", 0x2);
            tp_data("FF 80 25 00 00 25 00", 0x2);
            tp_data("00 30 00 00 30 00 00", 0x1);
        }
        if (f->data[3] == 0x21 && f->data[4] == 0x07) { // group 007: coolant 67.5
            tp_data("00 1A 61 07 05 09 8F", 0x2);
            tp_data("25 00 00 05 09 83 05", 0x2);
            tp_data("09 AF 25 00 00 25 00", 0x2);
            tp_data("00 30 00 00 30 00 00", 0x1);
        }
        if (f->data[3] == 0x21 && f->data[4] == 0x63) {
            tp_data("00 1A 61 63 01 73 24", 0x2);
            tp_data("05 3C 7F 05 3C 79 05", 0x2);
            tp_data("3C 78 25 00 00 25 00", 0x2);
            tp_data("00 30 00 00 30 00 00", 0x1);
        }
    }
}

static bool hw_send(void *ctx, const jd_frame_t *f)
{
    (void)ctx;
    TEST_ASSERT_TRUE_MESSAGE(jd_guard_check(f), "forbidden frame reached the hardware");
    if (bus.sent_count < 4096) bus.sent[bus.sent_count] = *f;
    bus.sent_count++;
    if (bus.kind == ECU_OBD) fake_obd(f);
    if (bus.kind == ECU_TP20) fake_tp20(f);
    return true;
}

// Runs the system for `ms` in 5 ms steps, delivering the engine's replies one step later.
static void run(uint32_t ms)
{
    for (uint32_t end = now + ms; now < end; now += 5) {
        jd_frame_t batch[32];
        int n = bus.pending_count;
        memcpy(batch, bus.pending, sizeof(jd_frame_t) * (size_t)n);
        bus.pending_count = 0;
        for (int i = 0; i < n; i++) jd_diag_on_frame(&d, &batch[i], now);
        jd_diag_tick(&d, now);
    }
}

static void start(ecu_kind_t kind, jd_mode_t mode, bool tx_enabled)
{
    memset(&bus, 0, sizeof bus);
    bus.kind = kind;
    now = 1000;
    jd_txgate_init(&gate, tx_enabled, hw_send, NULL);
    jd_diag_init(&d, mode, &gate, now);
}

static int count_sent(uint16_t id, uint8_t b0, uint8_t mask)
{
    int n = 0;
    for (int i = 0; i < bus.sent_count && i < 4096; i++)
        if (bus.sent[i].id == id && (bus.sent[i].data[0] & mask) == b0) n++;
    return n;
}

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------- passive

static void test_passive_decodes_vcds_traffic_and_sends_nothing(void)
{
    start(ECU_SILENT, JD_MODE_PASSIVE, false);
    const char *frames[][2] = {
        { "740", "1C 00 02 21 63" }, { "300", "BD" },
        { "300", "2A 00 1A 61 63 01 73 24" }, { "300", "2B 05 3C 7F 05 3C 79 05" },
        { "300", "2C 3C 78 25 00 00 25 00" }, { "300", "1D 00 30 00 00 30 00 00" },
        { "740", "BE" },
        { "7DF", "02 01 05 00 00 00 00 00" }, { "7E8", "03 41 05 6B 00 00 00 00" },
    };
    for (size_t i = 0; i < sizeof frames / sizeof frames[0]; i++) {
        jd_frame_t f = F((uint16_t)strtoul(frames[i][0], NULL, 16), frames[i][1]);
        jd_diag_on_frame(&d, &f, now);
    }
    run(2000);
    TEST_ASSERT_EQUAL(0, bus.sent_count);
    TEST_ASSERT_EQUAL(1620, d.store.r[JD_SIG_EGT].value.value);
    TEST_ASSERT_EQUAL(67, d.store.r[JD_SIG_COOLANT].value.value);
    TEST_ASSERT_EQUAL(JD_LINK_LISTEN, jd_diag_link(&d, now));
}

static void test_listen_only_build_never_transmits_in_any_mode(void)
{
    jd_mode_t modes[] = { JD_MODE_OBD, JD_MODE_TP20 };
    for (int m = 0; m < 2; m++) {
        start(ECU_OBD, modes[m], false);
        run(60000);
        TEST_ASSERT_EQUAL(0, bus.sent_count);
        TEST_ASSERT_EQUAL(0, gate.refused_listen_only); // it doesn't even try
        TEST_ASSERT_EQUAL(JD_LINK_LISTEN, jd_diag_link(&d, now));
    }
}

// ---------------------------------------------------------------- OBD-II

static void test_obd_reads_all_values(void)
{
    start(ECU_OBD, JD_MODE_OBD, true);
    run(3000);
    const jd_reading_t *egt = jd_store_get(&d.store, JD_SIG_EGT, now);
    const jd_reading_t *cool = jd_store_get(&d.store, JD_SIG_COOLANT, now);
    const jd_reading_t *rpm = jd_store_get(&d.store, JD_SIG_RPM, now);
    TEST_ASSERT_NOT_NULL(egt); TEST_ASSERT_NOT_NULL(cool); TEST_ASSERT_NOT_NULL(rpm);
    TEST_ASSERT_EQUAL(1727, egt->value.value);
    TEST_ASSERT_EQUAL(67, cool->value.value);
    TEST_ASSERT_EQUAL(82800, rpm->value.value);
    TEST_ASSERT_EQUAL(JD_LINK_OK, jd_diag_link(&d, now));
    TEST_ASSERT_EQUAL(JD_HALT_NONE, gate.halt);
}

static void test_obd_request_rate_is_gentle(void)
{
    start(ECU_OBD, JD_MODE_OBD, true);
    run(60000);
    int requests = count_sent(0x7E0, 0x02, 0xFF);
    TEST_ASSERT_TRUE(requests <= 60000 / JD_SCHED_MIN_GAP_MS + 1);
    TEST_ASSERT_TRUE_MESSAGE(requests >= 60, "should still poll about once a second");
    // EGT stays fresh the whole time
    TEST_ASSERT_NOT_NULL(jd_store_get(&d.store, JD_SIG_EGT, now));
}

static void test_probe_interval_is_short(void)
{
    // Sam, 2026-11-19: probing a silent ECU every 30 s is too slow. The display is on
    // an ignition-switched socket, so a silent ECU usually means it's still waking up.
    TEST_ASSERT_TRUE(JD_SCHED_QUIET_PROBE_MS <= 5000);
    TEST_ASSERT_TRUE(JD_DIAG_REOPEN_MAX_MS <= 5000);
}

static void test_obd_silent_ecu_goes_quiet(void)
{
    start(ECU_SILENT, JD_MODE_OBD, true);
    run(300000); // 5 minutes, ignition off
    int requests = count_sent(0x7E0, 0x02, 0xFF);
    // A few tries, then one probe every JD_SCHED_QUIET_PROBE_MS.
    TEST_ASSERT_TRUE(requests <= JD_SCHED_QUIET_AFTER + 300000 / JD_SCHED_QUIET_PROBE_MS + 1);
    TEST_ASSERT_EQUAL(JD_LINK_NO_ECU, jd_diag_link(&d, now));
    // Engine starts: polling recovers.
    bus.kind = ECU_OBD;
    run(JD_SCHED_QUIET_PROBE_MS + 3000);
    TEST_ASSERT_EQUAL(JD_LINK_OK, jd_diag_link(&d, now));
    TEST_ASSERT_NOT_NULL(jd_store_get(&d.store, JD_SIG_EGT, now));
}

static void test_bus_fault_stops_everything(void)
{
    start(ECU_OBD, JD_MODE_OBD, true);
    run(2000);
    int before = bus.sent_count;
    TEST_ASSERT_TRUE(before > 0);
    jd_txgate_bus_status(&gate, &(jd_bus_status_t){ .error_passive = true });
    run(60000);
    TEST_ASSERT_EQUAL(before, bus.sent_count);
    TEST_ASSERT_EQUAL(JD_LINK_HALTED, jd_diag_link(&d, now));
}

// ---------------------------------------------------------------- TP 2.0

static void test_tp20_reads_boost_coolant_and_egt(void)
{
    start(ECU_TP20, JD_MODE_TP20, true);
    run(3000);
    const jd_reading_t *act = jd_store_get(&d.store, JD_SIG_BOOST_ACT, now);
    TEST_ASSERT_NOT_NULL(act);
    TEST_ASSERT_EQUAL(20000, act->value.value);
    TEST_ASSERT_EQUAL(23000, jd_store_get(&d.store, JD_SIG_BOOST_SPEC, now)->value.value);
    TEST_ASSERT_EQUAL(675, jd_store_get(&d.store, JD_SIG_COOLANT, now)->value.value);
    // Group 099 field 2, EGT before turbo (Sam 2026-11-25: EGT back on screen).
    TEST_ASSERT_NOT_NULL(jd_store_get(&d.store, JD_SIG_EGT, now));
    TEST_ASSERT_EQUAL(1620, jd_store_get(&d.store, JD_SIG_EGT, now)->value.value);
    TEST_ASSERT_EQUAL(1, bus.setups);
    TEST_ASSERT_EQUAL(JD_LINK_OK, jd_diag_link(&d, now));
    TEST_ASSERT_EQUAL(JD_HALT_NONE, gate.halt);
}

static void test_tp20_request_rate_is_gentle(void)
{
    // Rule 4: a few requests per second. 011 every 500 ms + 099 every 2 s + 007 every 5 s = 2.7/s.
    start(ECU_TP20, JD_MODE_TP20, true);
    run(5000);
    int before = 0, egt_before = bus.group_reads[0x63];
    for (int g = 0; g < 256; g++) before += bus.group_reads[g];
    run(60000);
    int reads = -before;
    for (int g = 0; g < 256; g++) reads += bus.group_reads[g];
    // Planned 2.7/s; collisions with the 300 ms gap delay items (next turn counts from the send),
    // so the real rate is a little lower (~2.5/s), never higher.
    TEST_ASSERT_TRUE(reads <= 162 + 2);
    TEST_ASSERT_TRUE(reads >= 140);
    TEST_ASSERT_INT_WITHIN(3, 29, bus.group_reads[0x63] - egt_before); // EGT about every 2 s
}

static void test_tp20_opens_like_vcds(void)
{
    // VCDS: 10 89, 1A 9B, (31 B8: never), 1A 91, then 21 xx. We: 10 89, 1A 9B, 21 0B.
    start(ECU_TP20, JD_MODE_TP20, true);
    run(2000);
    const char *expect[] = { "10 89", "1A 9B", "21 0B" };
    int k = 0;
    for (int i = 0; i < bus.sent_count && k < 3; i++) {
        const jd_frame_t *f = &bus.sent[i];
        if (f->id != 0x740 || f->len < 5 || (f->data[0] >> 4) != 0x1) continue;
        uint8_t want[2];
        hex_bytes(expect[k], want, 2);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(want, &f->data[3], 2);
        k++;
    }
    TEST_ASSERT_EQUAL(3, k);
}

static void test_tp20_identification_refused_is_not_fatal(void)
{
    start(ECU_TP20, JD_MODE_TP20, true);
    bus.refuse_ident = true;
    run(3000);
    TEST_ASSERT_NOT_NULL(jd_store_get(&d.store, JD_SIG_BOOST_ACT, now));
    TEST_ASSERT_EQUAL(1, bus.setups);
}

static void test_tp20_long_run_stays_up(void)
{
    start(ECU_TP20, JD_MODE_TP20, true);
    run(120000);
    TEST_ASSERT_EQUAL(1, bus.setups);            // one channel the whole time
    TEST_ASSERT_TRUE(jd_tp20_is_open(&d.tp));
    TEST_ASSERT_NOT_NULL(jd_store_get(&d.store, JD_SIG_BOOST_ACT, now));
    TEST_ASSERT_NOT_NULL(jd_store_get(&d.store, JD_SIG_COOLANT, now));
    TEST_ASSERT_NOT_NULL(jd_store_get(&d.store, JD_SIG_EGT, now));
    // Rule 4: 011 every 0.5 s + 099 every 2 s + 007 every 5 s = 2.7 requests/s, +2 for the session/ident.
    int requests = count_sent(0x740, 0x10, 0xF0);
    TEST_ASSERT_TRUE(requests <= 120000 / 500 + 120000 / 2000 + 120000 / 5000 + 2 + 2);
    TEST_ASSERT_TRUE(bus.group_reads[0x63] >= 120000 / 2000 * 9 / 10);
    // Boost: ~1.86 reads/s with 099 in the mix (collisions with the 300 ms gap); the graph still
    // gets a point every UI refresh (200 ms) from the latest value.
    TEST_ASSERT_TRUE(bus.group_reads[0x0B] >= 120000 / 500 * 9 / 10);
    TEST_ASSERT_TRUE(bus.group_reads[0x07] >= 120000 / 5000 - 2);
    TEST_ASSERT_TRUE(bus.group_reads[0x07] <= 120000 / 5000 + 1);
    // A3 keeps coming while we poll, like VCDS (~every 0.55 s)
    TEST_ASSERT_TRUE_MESSAGE(bus.keepalives >= 120000 / 650, "keep-alive too rare while polling");
}

static void test_tp20_engine_off_retries_slowly(void)
{
    start(ECU_SILENT, JD_MODE_TP20, true);
    run(120000);
    int setups = count_sent(0x200, 0x01, 0xFF);
    // 0, 2.2, 6.4 s, then every REOPEN_MAX + setup timeout
    int max_setups = 3 + 120000 / (JD_DIAG_REOPEN_MAX_MS + JD_TP20_SETUP_TIMEOUT) + 1;
    TEST_ASSERT_TRUE(setups >= max_setups - 3);
    TEST_ASSERT_TRUE(setups <= max_setups);
    TEST_ASSERT_EQUAL(JD_LINK_NO_ECU, jd_diag_link(&d, now));
}

static void test_tp20_reconnects_after_engine_closes(void)
{
    start(ECU_TP20, JD_MODE_TP20, true);
    bus.drop_channel_once = true;
    run(10000);
    TEST_ASSERT_EQUAL(2, bus.setups);
    TEST_ASSERT_NOT_NULL(jd_store_get(&d.store, JD_SIG_BOOST_ACT, now));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_passive_decodes_vcds_traffic_and_sends_nothing);
    RUN_TEST(test_listen_only_build_never_transmits_in_any_mode);
    RUN_TEST(test_obd_reads_all_values);
    RUN_TEST(test_obd_request_rate_is_gentle);
    RUN_TEST(test_probe_interval_is_short);
    RUN_TEST(test_obd_silent_ecu_goes_quiet);
    RUN_TEST(test_bus_fault_stops_everything);
    RUN_TEST(test_tp20_reads_boost_coolant_and_egt);
    RUN_TEST(test_tp20_request_rate_is_gentle);
    RUN_TEST(test_tp20_opens_like_vcds);
    RUN_TEST(test_tp20_identification_refused_is_not_fatal);
    RUN_TEST(test_tp20_long_run_stays_up);
    RUN_TEST(test_tp20_engine_off_retries_slowly);
    RUN_TEST(test_tp20_reconnects_after_engine_closes);
    return UNITY_END();
}
