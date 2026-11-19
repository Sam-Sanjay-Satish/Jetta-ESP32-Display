// HARD RULES 1-3: only the exact allowlisted frames may be transmitted.
#include "jd/guard.h"
#include "test_util.h"

void setUp(void) {}
void tearDown(void) {}

static bool allowed(uint16_t id, const char *hex)
{
    jd_frame_t f = F(id, hex);
    return jd_guard_check(&f);
}

// ---- What must be allowed (exact bytes from the 2026-11-18 captures) ----

static void test_tp20_setup_to_engine_only(void)
{
    TEST_ASSERT_TRUE(allowed(0x200, "01 C0 00 10 00 03 01"));
    TEST_ASSERT_FALSE(allowed(0x200, "1F C0 00 10 00 03 01")); // gateway
    TEST_ASSERT_FALSE(allowed(0x200, "02 C0 00 10 00 03 01")); // DSG
    TEST_ASSERT_FALSE(allowed(0x200, "17 C0 00 10 00 03 01")); // cluster (rule 3)
}

static void test_tp20_channel_control(void)
{
    TEST_ASSERT_TRUE(allowed(0x740, "A0 0F 8A FF 32 FF"));
    TEST_ASSERT_TRUE(allowed(0x740, "A1 0F 8A FF 32 FF")); // answer to the module's A3
    TEST_ASSERT_TRUE(allowed(0x740, "A3"));
    TEST_ASSERT_TRUE(allowed(0x740, "A8"));
}

static void test_tp20_acks_any_sequence(void)
{
    for (int n = 0; n < 16; n++) {
        jd_frame_t f = jd_frame(0x740, (uint8_t[]){ (uint8_t)(0xB0 | n) }, 1);
        TEST_ASSERT_TRUE(jd_guard_check(&f));
    }
}

static void test_tp20_requests_any_sequence(void)
{
    for (int n = 0; n < 16; n++) {
        uint8_t session[] = { (uint8_t)(0x10 | n), 0x00, 0x02, 0x10, 0x89 };
        uint8_t group99[] = { (uint8_t)(0x10 | n), 0x00, 0x02, 0x21, 0x63 };
        jd_frame_t a = jd_frame(0x740, session, 5), b = jd_frame(0x740, group99, 5);
        TEST_ASSERT_TRUE(jd_guard_check(&a));
        TEST_ASSERT_TRUE(jd_guard_check(&b));
    }
}

static void test_engine_identification_and_fault_codes(void)
{
    // Approved by Sam 2026-11-19. Bytes as VCDS / macvagdiag sent them to the engine.
    for (int n = 0; n < 16; n++) {
        uint8_t ident[] = { (uint8_t)(0x10 | n), 0x00, 0x02, 0x1A, 0x9B };
        uint8_t dtcs[]  = { (uint8_t)(0x10 | n), 0x00, 0x04, 0x18, 0x00, 0xFF, 0x00 };
        jd_frame_t a = jd_frame(0x740, ident, sizeof ident), b = jd_frame(0x740, dtcs, sizeof dtcs);
        TEST_ASSERT_TRUE(jd_guard_check(&a));
        TEST_ASSERT_TRUE(jd_guard_check(&b));
    }
    // Freeze frame: exact prefix 12 00 04, exact length, any stored DTC number.
    TEST_ASSERT_TRUE(allowed(0x740, "13 00 05 12 00 04 04 01"));
    TEST_ASSERT_TRUE(allowed(0x740, "1F 00 05 12 00 04 2B A6"));
    TEST_ASSERT_TRUE(allowed(0x740, "10 00 05 12 00 04 FF FF"));
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 04 12 00 04 04"));       // short
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 05 12 01 04 04 01"));    // other record type
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 04 18 02 FF 00"));       // other modules' form, not the engine's
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 02 1A 90"));             // VIN: never read
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 02 1A 91"));
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 04 31 B8 00 00"));       // VCDS's routine: never
}

static void test_obd_mode01_physical_padded(void)
{
    TEST_ASSERT_TRUE(allowed(0x7E0, "02 01 05 00 00 00 00 00"));
    TEST_ASSERT_TRUE(allowed(0x7E0, "02 01 0C 00 00 00 00 00"));
    TEST_ASSERT_TRUE(allowed(0x7E0, "02 01 78 00 00 00 00 00"));
    TEST_ASSERT_TRUE(allowed(0x7E0, "30 00 01 00 00 00 00 00")); // ISO-TP flow control, as VCDS sent it
    TEST_ASSERT_FALSE(allowed(0x7E0, "30 00 00 00 00 00 00 00"));
}

// ---- What must be refused ----

static void test_functional_broadcast_refused(void)
{
    TEST_ASSERT_FALSE(allowed(0x7DF, "02 01 05 00 00 00 00 00")); // rule 3
    TEST_ASSERT_FALSE(allowed(0x7DF, "30 00 01 00 00 00 00 00"));
}

static void test_wrong_length_or_padding_refused(void)
{
    TEST_ASSERT_FALSE(allowed(0x7E0, "02 01 05"));
    TEST_ASSERT_FALSE(allowed(0x7E0, "02 01 05 00 00 00 00"));
    TEST_ASSERT_FALSE(allowed(0x7E0, "02 01 05 AA AA AA AA AA"));
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 02 21 63 00"));
    TEST_ASSERT_FALSE(allowed(0x740, "B1 00"));
    TEST_ASSERT_FALSE(allowed(0x740, ""));
}

static void test_approved_groups_allowed(void)
{
    // Approved 2026-11-19: 007 temperatures (coolant), 011 boost; 099 EGT since the start.
    TEST_ASSERT_TRUE(allowed(0x740, "10 00 02 21 07"));
    TEST_ASSERT_TRUE(allowed(0x740, "1F 00 02 21 0B"));
    TEST_ASSERT_TRUE(allowed(0x740, "13 00 02 21 63"));
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 02 21 0C"));
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 02 21 0B 00"));
}

static void test_forbidden_services_refused(void)
{
    // KWP2000 over TP 2.0 (rule 2)
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 02 10 85")); // programming session
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 02 10 86")); // extended/dev session
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 03 14 FF 00")); // clear DTCs
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 02 27 01")); // security access
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 02 11 01")); // ECU reset
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 02 31 01")); // routine
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 02 3B 9A")); // write data
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 02 30 01")); // IO control
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 02 28 01")); // comm control
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 02 85 02")); // DTC setting
    TEST_ASSERT_FALSE(allowed(0x740, "10 00 02 21 01")); // group not approved
    // OBD-II / UDS on 7E0
    TEST_ASSERT_FALSE(allowed(0x7E0, "01 04 00 00 00 00 00 00")); // mode 04 clear codes
    TEST_ASSERT_FALSE(allowed(0x7E0, "02 10 02 00 00 00 00 00")); // UDS programming
    TEST_ASSERT_FALSE(allowed(0x7E0, "02 27 01 00 00 00 00 00"));
    TEST_ASSERT_FALSE(allowed(0x7E0, "02 01 5C 00 00 00 00 00")); // not on the list
}

static void test_tp20_multi_packet_ops_refused(void)
{
    // Only single-packet "last, ACK wanted" requests: op nibble must be 1.
    TEST_ASSERT_FALSE(allowed(0x740, "00 00 02 21 63"));
    TEST_ASSERT_FALSE(allowed(0x740, "20 00 02 21 63"));
    TEST_ASSERT_FALSE(allowed(0x740, "30 00 02 21 63"));
    TEST_ASSERT_FALSE(allowed(0x740, "90"));             // we never say "not ready"
}

static void test_oversized_frame_refused(void)
{
    jd_frame_t f = F(0x7E0, "02 01 05 00 00 00 00 00");
    f.len = 9;
    TEST_ASSERT_NULL(jd_guard_match(&f));
}

// ---- Properties of the list itself ----

static void test_every_fixed_bit_matters(void)
{
    // Flipping any bit the mask fixes, or changing the length or ID, is refused.
    for (size_t i = 0; i < jd_allowlist_len; i++) {
        const jd_allow_entry_t *e = &jd_allowlist[i];
        jd_frame_t ok = jd_frame(e->id, e->data, e->len);
        TEST_ASSERT_TRUE_MESSAGE(jd_guard_check(&ok), e->name);
        for (int b = 0; b < e->len; b++) {
            for (int bit = 0; bit < 8; bit++) {
                if (!(e->mask[b] & (1u << bit))) continue;
                jd_frame_t bad = ok;
                bad.data[b] ^= (uint8_t)(1u << bit);
                // The flipped frame must not pass as this entry. (It may be another real
                // frame: A0 and A1 differ in one bit and both are allowed.)
                TEST_ASSERT_TRUE_MESSAGE(jd_guard_match(&bad) != e, e->name);
            }
        }
        jd_frame_t shorter = ok, longer = ok, other_id = ok;
        if (shorter.len) shorter.len--;
        if (longer.len < 8) longer.len++;
        other_id.id ^= 1;
        TEST_ASSERT_NULL_MESSAGE(jd_guard_match(&other_id), e->name);
        if (ok.len) TEST_ASSERT_TRUE_MESSAGE(jd_guard_match(&shorter) != e, e->name);
        if (ok.len < 8) TEST_ASSERT_TRUE_MESSAGE(jd_guard_match(&longer) != e, e->name);
    }
}

static void test_list_shape(void)
{
    TEST_ASSERT_TRUE(jd_allowlist_len > 0);
    for (size_t i = 0; i < jd_allowlist_len; i++) {
        const jd_allow_entry_t *e = &jd_allowlist[i];
        // Rule 3: engine only (TP 2.0 setup 200, engine channel 740, OBD-II physical 7E0).
        TEST_ASSERT_TRUE_MESSAGE(e->id == 0x200 || e->id == 0x740 || e->id == 0x7E0, e->name);
        TEST_ASSERT_TRUE(e->len >= 1 && e->len <= 8);
        for (int b = 0; b < e->len; b++) {
            // Only byte 0 of a TP 2.0 frame may have a free (sequence) nibble, and only the
            // DTC number of the freeze-frame request (12 00 04 [hi] [lo]) may be free bytes.
            bool freeze_dtc = e->id == 0x740 && e->len == 8 && e->data[3] == 0x12 && b >= 6;
            if (b == 0 && e->id == 0x740) TEST_ASSERT_TRUE(e->mask[b] == 0xFF || e->mask[b] == 0xF0);
            else if (freeze_dtc) TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, e->mask[b], e->name);
            else TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, e->mask[b], e->name);
            TEST_ASSERT_EQUAL_HEX8(e->data[b], e->data[b] & e->mask[b]);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tp20_setup_to_engine_only);
    RUN_TEST(test_tp20_channel_control);
    RUN_TEST(test_tp20_acks_any_sequence);
    RUN_TEST(test_tp20_requests_any_sequence);
    RUN_TEST(test_engine_identification_and_fault_codes);
    RUN_TEST(test_obd_mode01_physical_padded);
    RUN_TEST(test_functional_broadcast_refused);
    RUN_TEST(test_wrong_length_or_padding_refused);
    RUN_TEST(test_approved_groups_allowed);
    RUN_TEST(test_forbidden_services_refused);
    RUN_TEST(test_tp20_multi_packet_ops_refused);
    RUN_TEST(test_oversized_frame_refused);
    RUN_TEST(test_every_fixed_bit_matters);
    RUN_TEST(test_list_shape);
    return UNITY_END();
}
