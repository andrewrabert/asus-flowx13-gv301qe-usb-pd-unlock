#include "ec_entry_probe.h"
struct entry_session {
    const struct ec_io *io;
    struct entry_report *r;
    uint64_t start;
    unsigned phase;
};
static void trace(struct entry_session *s, unsigned event, uint8_t value) {
    struct entry_report *r=s->r;
    if (r->trace_count < ENTRY_TRACE_MAX)
        r->trace[r->trace_count++]=(struct entry_trace){
            s->io->us(s->io->ctx)-s->start,s->phase,event,value};
}
static int wait_for(struct entry_session *s, uint8_t mask, uint8_t value) {
    uint64_t start=s->io->us(s->io->ctx);
    uint8_t status=s->io->status(s->io->ctx);
    trace(s,ENTRY_STATUS,status);
    for (unsigned n=0;n<1000000;++n) {
        s->r->final_status=status;
        if (status==0xff) break;
        if ((status&mask)==value) { trace(s,ENTRY_WAIT_OK,status); return 1; }
        if (s->io->us(s->io->ctx)-start>=25000) break;
        status=s->io->status(s->io->ctx);
    }
    trace(s,ENTRY_WAIT_FAIL,status);
    return 0;
}
void ec_entry_probe(const struct ec_io *io, struct entry_report *r) {
    *r=(struct entry_report){0};
    struct entry_session s={io,r,io->us(io->ctx),ENTRY_INITIAL};
    r->initial_status=r->final_status=io->status(io->ctx);
    trace(&s,ENTRY_STATUS,r->initial_status);
    if (r->initial_status==0xff || (r->initial_status&3)) {
        r->result=EC_BUSY; goto done;
    }
    r->result=EC_TIMEOUT;
    s.phase=ENTRY_PRE_DC;
    if (!wait_for(&s,2,0)) goto failed;
    io->command(io->ctx,0xdc); r->entry_sent=1;
    trace(&s,ENTRY_WRITE,0xdc);
    s.phase=ENTRY_POST_DC;
    if (!wait_for(&s,2,0)) goto failed;
    s.phase=ENTRY_ACK;
    if (!wait_for(&s,1,1)) goto failed;
    r->ack=io->data(io->ctx); trace(&s,ENTRY_DATA,r->ack);
    if (r->ack!=0x33) { r->result=EC_BAD_ACK; goto failed; }
    r->ack_seen=1;
    r->result=EC_OK;
    goto cleanup;
failed:
    r->failure_phase=s.phase;
cleanup:
    /* No forwarding prefix or follow mode was sent: FC directly is sufficient.
     * Never inject 05, flash opcodes, or retries into this diagnostic. */
    if (r->entry_sent) {
        s.phase=ENTRY_PRE_FC;
        if (wait_for(&s,2,0)) {
            io->command(io->ctx,0xfc); r->exit_sent=1;
            trace(&s,ENTRY_WRITE,0xfc);
            s.phase=ENTRY_POST_FC;
            r->exit_consumed=wait_for(&s,2,0);
        }
        if (!r->exit_consumed) {
            r->cleanup_failure_phase=s.phase;
            if (r->result==EC_OK) r->result=EC_TIMEOUT;
        }
    }
done:
    r->elapsed_us=io->us(io->ctx)-s.start;
    /* Consumption of FC is not proof that EC runtime has recovered. */
}
