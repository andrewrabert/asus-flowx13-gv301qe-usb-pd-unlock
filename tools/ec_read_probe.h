#ifndef EC_READ_PROBE_H
#define EC_READ_PROBE_H
#include <stdint.h>
/* No caller-selected opcodes, addresses, erase, program, or write-enable API. */
#define EC_SAMPLE_SIZE 96
#define EC_READ_TRACE_MAX 512
enum ec_read_stage { EC_READ_PREWAIT = 1, EC_READ_POSTWAIT, EC_READ_RESPONSE };
struct ec_read_trace {
    uint64_t started_us, elapsed_us;
    unsigned cleanup, sent, complete, stage;
    uint8_t command, status;
};
struct ec_io {
    void *ctx;
    uint8_t (*status)(void *);
    uint8_t (*data)(void *);
    void (*command)(void *, uint8_t);
    uint64_t (*us)(void *);
};
enum ec_result { EC_OK, EC_BUSY, EC_TIMEOUT, EC_BAD_ACK, EC_MISMATCH };
struct ec_report {
    enum ec_result result;
    unsigned entry_sent, ack_seen, exit_consumed, cleanup_failed;
    unsigned commands, sample_bytes;
    uint8_t initial_status, ack, id[2][3], sample[2][EC_SAMPLE_SIZE];
    unsigned primary_commands, cleanup_commands;
    /* Failure indices are one-based; zero means none. */
    unsigned primary_failure, cleanup_failure, trace_count, trace_dropped;
    struct ec_read_trace trace[EC_READ_TRACE_MAX];
};
void ec_read_probe(const struct ec_io *io, struct ec_report *report);
#endif
