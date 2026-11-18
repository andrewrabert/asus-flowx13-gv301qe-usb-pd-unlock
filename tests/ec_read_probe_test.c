#include "../tools/ec_read_probe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct fake {
    uint64_t time, busy_until;
    unsigned writes, fault_at, response_fault, reads, pending, follow, spi, argc, index;
    unsigned entered, exited, bad_ack, mismatch, initial, frozen, stuck;
    uint32_t addr;
    uint8_t output;
    unsigned obf;
};
static uint64_t us(void *p) { struct fake *f=p; if (!f->frozen) f->time+=10; return f->time; }
static uint8_t status(void *p) {
    struct fake *f=p;
    if (f->initial && !f->writes) return f->initial;
    if (f->stuck || f->time < f->busy_until) return 10;
    return 8 | f->obf;
}
static uint8_t data(void *p) { struct fake *f=p; assert(f->obf); f->obf=0; return f->output; }
static void command(void *p, uint8_t c) {
    struct fake *f=p;
    assert(!f->stuck && f->time >= f->busy_until);
    ++f->writes;
    if (!f->entered) {
        assert(c==0xdc); f->entered=1; f->output=f->bad_ack?0x44:0x33; f->obf=1;
    } else if (f->pending) {
        unsigned prefix=f->pending; f->pending=0;
        if (prefix==2) {
            /* Only read commands can reach the SPI bus, including cleanup. */
            assert(c==0x9f || c==0x0b || c==5);
            f->spi=c; f->argc=0; f->index=0; f->addr=0;
        } else {
            assert(f->spi==0x0b);
            if (f->argc < 3) f->addr=(f->addr<<8)|c;
            ++f->argc;
        }
    } else switch(c) {
        case 1: f->follow=1; break;
        case 2: case 3: assert(f->follow); f->pending=c; break;
        case 4:
            assert(f->follow);
            if (f->spi==0x9f) f->output=(uint8_t)(0xef+f->index++);
            else {
                assert(f->spi==0x0b && f->argc==4);
                f->output=(uint8_t)((f->addr+f->index++) ^ 0xa5);
            }
            if (f->mismatch && f->reads==103) f->output^=1;
            ++f->reads;
            f->obf=(f->reads!=f->response_fault);
            break;
        case 5: f->follow=0; break;
        case 0xfc: assert(!f->follow); f->exited=1; break;
        default: assert(!"unexpected command");
    }
    if (f->writes==f->fault_at) f->busy_until=f->time+30000;
}
static struct ec_report run(struct fake *f) {
    const struct ec_io io={f,status,data,command,us}; struct ec_report r;
    ec_read_probe(&io,&r); assert(f->time<700000); return r;
}
int main(void) {
    struct fake f={0}; struct ec_report r=run(&f);
    assert(r.result==EC_OK && r.sample_bytes==192 && r.exit_consumed && !r.cleanup_failed && f.exited);
    assert(!r.trace_dropped && r.trace_count==r.commands);
    assert(!r.primary_failure && !r.cleanup_failure && r.cleanup_commands==3);
    assert(r.primary_commands+r.cleanup_commands==r.commands);
    for (unsigned i=0;i<r.trace_count;i++) {
        assert(r.trace[i].sent && r.trace[i].complete);
        assert(r.trace[i].cleanup==(i>=r.trace_count-3));
        if(i) assert(r.trace[i].started_us>=r.trace[i-1].started_us+r.trace[i-1].elapsed_us);
    }
    unsigned total=f.writes;
    for (unsigned k=1;k<=total;k++) {
        f=(struct fake){.fault_at=k}; r=run(&f);
        /* A fault on final FC may obscure acknowledgement, but FC was sent. */
        assert(f.exited);
        assert(r.result!=EC_OK || r.cleanup_failed);
        unsigned index = k<=total-3 ? r.primary_failure : r.cleanup_failure;
        assert(index && !r.trace_dropped);
        const struct ec_read_trace *t=&r.trace[index-1];
        assert(t->sent && !t->complete && t->stage==EC_READ_POSTWAIT);
        assert(t->status==10 && t->elapsed_us>=25000);
        assert(t->cleanup==(k>total-3));
    }
    for (unsigned k=1;k<=198;k++) {
        f=(struct fake){.response_fault=k}; r=run(&f);
        assert(r.result==EC_TIMEOUT && f.exited);
        assert(r.primary_failure && !r.cleanup_failure);
        const struct ec_read_trace *t=&r.trace[r.primary_failure-1];
        assert(t->command==4 && t->stage==EC_READ_RESPONSE && t->sent && !t->complete);
    }
    f=(struct fake){.bad_ack=1}; r=run(&f);
    assert(r.result==EC_BAD_ACK && f.exited && f.reads==0);
    f=(struct fake){.mismatch=1}; r=run(&f);
    assert(r.result==EC_MISMATCH && f.exited);
    for (unsigned k=1;k<=3;k++) {
        f=(struct fake){.initial=k}; r=run(&f); assert(r.result==EC_BUSY && !f.writes);
    }
    f=(struct fake){.initial=255}; r=run(&f); assert(r.result==EC_BUSY && !f.writes);
    f=(struct fake){.frozen=1,.fault_at=1}; r=run(&f);
    assert(r.result==EC_TIMEOUT && r.cleanup_failed && !f.exited);
    assert(r.primary_failure==1 && r.cleanup_failure==2);
    assert(r.trace[0].sent && !r.trace[1].sent);
    assert(r.trace[1].stage==EC_READ_PREWAIT && r.trace[1].cleanup);
    printf("PASS: normal read, %u command timeout points, 198 response timeouts, bad ACK, mismatch, busy/absent EC, frozen clock\n",total);
}
