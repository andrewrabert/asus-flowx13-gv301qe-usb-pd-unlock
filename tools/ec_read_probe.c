#include "ec_read_probe.h"
#include <stddef.h>
struct session {
    const struct ec_io *io; struct ec_report *r;
    uint64_t start, budget, origin;
    unsigned cleanup;
    struct ec_read_trace *active;
};
static int finish_trace(struct session *s, int success) {
    if (s->active) {
        s->active->elapsed_us = s->io->us(s->io->ctx) - s->origin - s->active->started_us;
        s->active->complete = success;
        unsigned *failure = s->cleanup ? &s->r->cleanup_failure : &s->r->primary_failure;
        if (!success && !*failure) *failure = s->r->trace_count;
    }
    return success;
}
static int expired(struct session *s, uint64_t start, uint64_t limit) {
    uint64_t now = s->io->us(s->io->ctx);
    return now - start >= limit || now - s->start >= s->budget;
}
static int wait_status(struct session *s, uint8_t mask, uint8_t value) {
    uint64_t start = s->io->us(s->io->ctx);
    /* Iteration limit also terminates if the time source stops advancing. */
    for (unsigned n = 0; n < 1000000; ++n) {
        if (expired(s, start, 25000)) return 0;
        uint8_t status = s->io->status(s->io->ctx);
        if (s->active) s->active->status = status;
        if (status == 0xff) return 0;
        if ((status & mask) == value) return 1;
    }
    return 0;
}
static int command(struct session *s, uint8_t c) {
    s->active = NULL;
    if (s->r->trace_count < EC_READ_TRACE_MAX) {
        s->active = &s->r->trace[s->r->trace_count++];
        *s->active = (struct ec_read_trace){
            .started_us = s->io->us(s->io->ctx) - s->origin,
            .cleanup = s->cleanup, .command = c, .stage = EC_READ_PREWAIT,
            .status = 0xff
        };
    } else {
        ++s->r->trace_dropped;
    }
    if (!wait_status(s, 2, 0)) return finish_trace(s, 0);
    s->io->command(s->io->ctx, c);
    ++s->r->commands;
    if (s->cleanup) ++s->r->cleanup_commands;
    else ++s->r->primary_commands;
    if (s->active) { s->active->sent = 1; s->active->stage = EC_READ_POSTWAIT; }
    if (c == 0xdc) s->r->entry_sent = 1; /* Track before waiting for ACK. */
    return finish_trace(s, wait_status(s, 2, 0));
}
static int response(struct session *s, uint8_t *v) {
    if (s->active) s->active->stage = EC_READ_RESPONSE;
    if (!wait_status(s, 1, 1)) return finish_trace(s, 0);
    *v = s->io->data(s->io->ctx);
    return finish_trace(s, 1);
}
static int forward(struct session *s, uint8_t prefix, uint8_t byte) {
    return command(s, prefix) && command(s, byte);
}
static int read_byte(struct session *s, uint8_t *v) {
    return command(s, 4) && response(s, v);
}
static int read_id(struct session *s, uint8_t *id) {
    if (!command(s, 1) || !forward(s, 2, 0x9f)) return 0;
    for (unsigned i = 0; i < 3; ++i) if (!read_byte(s, id+i)) return 0;
    return command(s, 5);
}
static int read_sample(struct session *s, uint8_t *sample) {
    static const uint32_t address[] = {0, 0x80, 0xc5e0, 0x11ef0};
    static const unsigned length[] = {32, 32, 16, 16};
    unsigned out = 0;
    for (unsigned k = 0; k < 4; ++k) {
        if (!command(s, 1) || !forward(s, 2, 0x0b)) return 0;
        for (int shift = 16; shift >= 0; shift -= 8)
            if (!forward(s, 3, (uint8_t)(address[k] >> shift))) return 0;
        if (!forward(s, 3, 0)) return 0; /* FAST_READ dummy byte. */
        for (unsigned i = 0; i < length[k]; ++i) {
            if (!read_byte(s, sample+out++)) return 0;
            ++s->r->sample_bytes;
        }
        if (!command(s, 5)) return 0;
    }
    return 1;
}
void ec_read_probe(const struct ec_io *io, struct ec_report *r) {
    *r = (struct ec_report){0};
    uint64_t begin = io->us(io->ctx);
    struct session s = {.io=io, .r=r, .start=begin, .origin=begin, .budget=500000};
    r->initial_status = io->status(io->ctx);
    /* Never drain another owner's response or overwrite pending input. */
    if (r->initial_status == 0xff || (r->initial_status & 3)) {
        r->result = EC_BUSY; return;
    }
    r->result = EC_TIMEOUT;
    if (!command(&s, 0xdc) || !response(&s, &r->ack)) goto cleanup;
    if (r->ack != 0x33) { r->result = EC_BAD_ACK; goto cleanup; }
    r->ack_seen = 1;
    for (unsigned pass = 0; pass < 2; ++pass)
        if (!read_id(&s, r->id[pass]) || !read_sample(&s, r->sample[pass])) goto cleanup;
    r->result = EC_OK;
    for (unsigned i = 0; i < EC_SAMPLE_SIZE; ++i)
        if (r->sample[0][i] != r->sample[1][i]) r->result = EC_MISMATCH;
    for (unsigned i = 0; i < 3; ++i)
        if (r->id[0][i] != r->id[1][i]) r->result = EC_MISMATCH;
cleanup:
    if (!r->entry_sent) return;
    s.cleanup = 1;
    s.start = io->us(io->ctx); s.budget = 150000;
    /* If interrupted after a forwarding prefix, first 05 is consumed as data
     * (or read-status opcode); second 05 exits follow mode. No write enable was
     * issued. This is best effort, NOT recovery from arbitrary interference. */
    int first = command(&s, 5);
    int second = command(&s, 5);
    int last = command(&s, 0xfc);
    r->exit_consumed = last;
    r->cleanup_failed = !(first && second && last);
    /* IBF clear means consumed, not proof of resumed EC thermal management. */
}
