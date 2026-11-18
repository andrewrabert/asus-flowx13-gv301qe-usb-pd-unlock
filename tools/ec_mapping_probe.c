#include "ec_mapping_probe.h"
struct mapping_session { const struct mapping_io *io; struct mapping_report *r; uint64_t start; };
/* Results: 0 success, 1 initially busy/absent, 2 timeout/absent,
 * 3 unexpected output, 4 unequal passes. No recovery writes are attempted. */
static int wait_for(struct mapping_session *s, unsigned response, unsigned forbid_output) {
    uint64_t begin=s->io->us(s->io->ctx);
    for(unsigned n=0;n<1000000;++n) {
        uint8_t v=s->io->status(s->io->ctx); s->r->final=v;
        if(v==0xff) break;
        if(forbid_output && (v&1)) { s->r->result=3; return 0; }
        if(!(v&2) && (!response || (v&1))) return 1;
        uint64_t now=s->io->us(s->io->ctx);
        if(now-begin>=25000 || now-s->start>=500000) break;
    }
    s->r->result=2; return 0;
}
void ec_mapping_probe(const struct mapping_io *io, struct mapping_report *r) {
    *r=(struct mapping_report){0};
    struct mapping_session s={io,r,io->us(io->ctx)};
    r->initial=r->final=io->status(io->ctx);
    if(r->initial==0xff || (r->initial&3)) { r->result=1; goto done; }
    for(unsigned i=0;i<MAP_COUNT*MAP_PASSES;++i) {
        struct mapping_row *row=&r->row[i];
        unsigned k=i%MAP_COUNT;
        row->address=k?0x1040+k-1:0x019f;
        uint64_t begin=io->us(io->ctx);
        ++r->rows;
        for(unsigned phase=0;phase<5;++phase) {
            row->phase=phase;
            int ok=wait_for(&s,phase==3,phase!=3);
            row->status[phase]=r->final;
            if(!ok) { row->elapsed=io->us(io->ctx)-begin; goto done; }
            if(phase==0) {
                r->incomplete=1; io->command(io->ctx,0x92); ++r->writes;
            } else if(phase==1) {
                io->write_data(io->ctx,row->address>>8); ++r->writes;
            } else if(phase==2) {
                io->write_data(io->ctx,row->address&255); ++r->writes;
            } else if(phase==3) {
                row->value=io->data(io->ctx); ++r->reads;
            }
        }
        row->valid=1; r->incomplete=0;
        row->elapsed=io->us(io->ctx)-begin;
    }
    for(unsigned i=0;i<MAP_COUNT;++i)
        if(r->row[i].value!=r->row[i+MAP_COUNT].value) r->result=4;
done:
    r->elapsed=io->us(io->ctx)-s.start;
}
