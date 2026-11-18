#include "../tools/ec_entry_probe.h"
#include <assert.h>
#include <stdio.h>
struct fake {
    uint64_t time;
    unsigned writes, reads, statuses, initial, freeze, missing_ack, bad_ack;
    unsigned stuck_after, pre_dc_busy, obf;
    uint8_t sent[2];
};
static uint64_t us(void *p) { struct fake *f=p; if(!f->freeze) f->time+=10; return f->time; }
static uint8_t status(void *p) {
    struct fake *f=p;
    ++f->statuses;
    if(!f->writes && f->initial) return f->initial;
    if(f->pre_dc_busy && f->statuses>1 && !f->writes) return 0x0a;
    if(f->stuck_after && f->writes>=f->stuck_after) return 0x0a;
    return 8|f->obf;
}
static uint8_t data(void *p) {
    struct fake *f=p; assert(f->obf); f->obf=0; ++f->reads;
    return f->bad_ack?0x44:0x33;
}
static void command(void *p,uint8_t c) {
    struct fake *f=p;
    assert(f->writes<2);
    assert(c==(f->writes?0xfc:0xdc));
    f->sent[f->writes++]=c;
    if(c==0xdc && !f->missing_ack) f->obf=1;
}
static struct entry_report run(struct fake *f) {
    const struct ec_io io={f,status,data,command,us}; struct entry_report r;
    ec_entry_probe(&io,&r);
    assert(r.trace_count<ENTRY_TRACE_MAX && r.elapsed_us<150000);
    assert(f->writes<=2 && f->reads<=1);
    unsigned traced_writes=0;
    for(unsigned i=0;i<r.trace_count;++i) {
        if(i) assert(r.trace[i].us>=r.trace[i-1].us);
        if(r.trace[i].event==ENTRY_WRITE)
            assert(r.trace[i].value==f->sent[traced_writes++]);
    }
    assert(traced_writes==f->writes);
    return r;
}
int main(void) {
    struct fake f={0}; struct entry_report r=run(&f);
    assert(r.result==EC_OK && r.ack_seen && r.exit_consumed && f.writes==2);
    for(unsigned s=1;s<256;++s) if(s==255 || (s&3)) {
        f=(struct fake){.initial=s}; r=run(&f);
        assert(r.result==EC_BUSY && !f.writes);
    }
    f=(struct fake){.pre_dc_busy=1}; r=run(&f);
    assert(!r.entry_sent && r.failure_phase==ENTRY_PRE_DC && !f.writes);
    f=(struct fake){.stuck_after=1}; r=run(&f);
    assert(r.failure_phase==ENTRY_POST_DC && r.cleanup_failure_phase==ENTRY_PRE_FC);
    assert(!r.exit_sent && f.writes==1);
    f=(struct fake){.stuck_after=2}; r=run(&f);
    assert(r.ack_seen && r.exit_sent && !r.exit_consumed);
    assert(r.cleanup_failure_phase==ENTRY_POST_FC && r.result==EC_TIMEOUT);
    f=(struct fake){.missing_ack=1}; r=run(&f);
    assert(r.failure_phase==ENTRY_ACK && r.result==EC_TIMEOUT && r.exit_consumed);
    f=(struct fake){.bad_ack=1}; r=run(&f);
    assert(r.failure_phase==ENTRY_ACK && r.result==EC_BAD_ACK && r.exit_consumed);
    f=(struct fake){.freeze=1,.stuck_after=1}; r=run(&f);
    assert(r.result==EC_TIMEOUT && !r.exit_consumed && f.writes==1);
    puts("PASS: DC/FC only; entry/ACK/exit failures; busy/absent EC; trace integrity; frozen clock");
}
