// The fixed read-only allowlist. Adding an entry needs Sam's approval (HARD RULES).
// Sources: TP 2.0 / OBD-II captures of 2026-11-13 and 2026-11-18 (see CLAUDE.md).
#include "jd/guard.h"

#define EXACT {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define SEQ   {0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF} // byte 0 low nibble = TP 2.0 sequence

const jd_allow_entry_t jd_allowlist[] = {
    // --- VW TP 2.0 to the engine ECU (logical address 01) ---
    { "tp20 setup engine",  0x200, 7, {0x01, 0xC0, 0x00, 0x10, 0x00, 0x03, 0x01}, EXACT },
    { "tp20 timing A0",     0x740, 6, {0xA0, 0x0F, 0x8A, 0xFF, 0x32, 0xFF},       EXACT },
    { "tp20 timing A1",     0x740, 6, {0xA1, 0x0F, 0x8A, 0xFF, 0x32, 0xFF},       EXACT },
    { "tp20 keep-alive A3", 0x740, 1, {0xA3},                                     EXACT },
    { "tp20 close A8",      0x740, 1, {0xA8},                                     EXACT },
    { "tp20 ACK Bn",        0x740, 1, {0xB0},                                     SEQ },
    { "kwp session 10 89",  0x740, 5, {0x10, 0x00, 0x02, 0x10, 0x89},             SEQ },
    { "kwp group 099 EGT",  0x740, 5, {0x10, 0x00, 0x02, 0x21, 0x63},             SEQ },
    // Approved 2026-11-19: group 007 (temperatures: coolant) and 011 (boost specified/actual).
    { "kwp group 007 temps", 0x740, 5, {0x10, 0x00, 0x02, 0x21, 0x07},            SEQ },
    { "kwp group 011 boost", 0x740, 5, {0x10, 0x00, 0x02, 0x21, 0x0B},            SEQ },
    // Approved 2026-11-19: identification (VCDS sends it before group reads), engine fault codes
    // and freeze frames (bytes as macvagdiag reads them on this car).
    { "kwp ident 1A 9B",    0x740, 5, {0x10, 0x00, 0x02, 0x1A, 0x9B},             SEQ },
    { "kwp DTCs 18 00 FF 00", 0x740, 7, {0x10, 0x00, 0x04, 0x18, 0x00, 0xFF, 0x00}, SEQ },
    { "kwp freeze frame 12 00 04 hi lo", 0x740, 8, {0x10, 0x00, 0x05, 0x12, 0x00, 0x04, 0x00, 0x00},
                                        {0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00} }, // any DTC number

    // --- OBD-II mode 01 on the engine's physical ID (ISO-TP, padded 00) ---
    { "obd 01 05 coolant",  0x7E0, 8, {0x02, 0x01, 0x05, 0, 0, 0, 0, 0},          EXACT },
    { "obd 01 0C rpm",      0x7E0, 8, {0x02, 0x01, 0x0C, 0, 0, 0, 0, 0},          EXACT },
    { "obd 01 78 egt",      0x7E0, 8, {0x02, 0x01, 0x78, 0, 0, 0, 0, 0},          EXACT },
    { "isotp flow control", 0x7E0, 8, {0x30, 0x00, 0x01, 0, 0, 0, 0, 0},          EXACT }, // as VCDS sent it
};
const size_t jd_allowlist_len = sizeof jd_allowlist / sizeof jd_allowlist[0];

static bool entry_matches(const jd_allow_entry_t *e, const jd_frame_t *f)
{
    if (e->id != f->id || e->len != f->len) return false;
    for (int i = 0; i < f->len; i++) {
        if ((f->data[i] & e->mask[i]) != e->data[i]) return false;
    }
    return true;
}

const jd_allow_entry_t *jd_guard_match(const jd_frame_t *frame)
{
    if (!frame || frame->len > 8 || frame->id > 0x7FF) return NULL;
    for (size_t i = 0; i < jd_allowlist_len; i++) {
        if (entry_matches(&jd_allowlist[i], frame)) return &jd_allowlist[i];
    }
    return NULL;
}
