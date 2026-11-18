#ifndef EC_ENTRY_PROBE_H
#define EC_ENTRY_PROBE_H
#include "ec_read_probe.h"
enum entry_phase { ENTRY_INITIAL, ENTRY_PRE_DC, ENTRY_POST_DC, ENTRY_ACK,
                   ENTRY_PRE_FC, ENTRY_POST_FC, ENTRY_DONE };
enum entry_event { ENTRY_STATUS, ENTRY_WRITE, ENTRY_DATA, ENTRY_WAIT_OK, ENTRY_WAIT_FAIL };
struct entry_trace {
    uint64_t us;
    unsigned phase, event;
    uint8_t value;
};
#define ENTRY_TRACE_MAX 24
struct entry_report {
    unsigned result, entry_sent, ack_seen, exit_sent, exit_consumed;
    unsigned failure_phase, cleanup_failure_phase, trace_count;
    uint8_t initial_status, final_status, ack;
    uint64_t elapsed_us;
    struct entry_trace trace[ENTRY_TRACE_MAX];
};
void ec_entry_probe(const struct ec_io *, struct entry_report *);
#endif
