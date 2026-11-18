#include "ec_indirect_probe.h"
struct session { const struct mapping_io *io; struct indirect_report *r; uint64_t start; };
static int wait_for(struct session *s, unsigned response, unsigned forbid) {
    uint64_t t=s->io->us(s->io->ctx);
    for(unsigned n=0;n<1000000;++n) {
        uint8_t v=s->io->status(s->io->ctx); s->r->status=v;
        if(v==255) break;
        if(forbid && (v&1)) { s->r->result=3; return 0; }
        if(!(v&2) && (!response || (v&1))) return 1;
        uint64_t now=s->io->us(s->io->ctx);
        if(now-t>=25000 || now-s->start>=2000000) break;
    }
    s->r->result=2; return 0;
}
static int reg_access(struct session *s,unsigned address,uint8_t *value,unsigned write) {
    /* No write to ECINDDR, control, mapping, or flash-follow-mode registers.
     * ECINDAR3 writes have follow-mode nibble zero, including restoration. */
    if(write && (address<0x103b || address>0x103e ||
                 (address==0x103e && *value!=0 && *value!=0x40))) {
        s->r->result=5; return 0;
    }
    s->r->current_address=address;
    if(!wait_for(s,0,1)) return 0;
    s->r->incomplete=1;
    s->io->command(s->io->ctx,write?0x93:0x92); ++s->r->writes;
    if(!wait_for(s,0,1)) return 0;
    s->io->write_data(s->io->ctx,address>>8); ++s->r->writes;
    if(!wait_for(s,0,1)) return 0;
    s->io->write_data(s->io->ctx,address&255); ++s->r->writes;
    if(write) {
        if(!wait_for(s,0,1)) return 0;
        s->io->write_data(s->io->ctx,*value); ++s->r->writes;
        if(!wait_for(s,0,1)) return 0;
    } else {
        if(!wait_for(s,1,0)) return 0;
        *value=s->io->data(s->io->ctx); ++s->r->reads;
        if(!wait_for(s,0,1)) return 0;
    }
    s->r->incomplete=0;
    return 1;
}
static int put(struct session *s,unsigned a,uint8_t v) { return reg_access(s,a,&v,1); }
void ec_indirect_probe(const struct mapping_io *io,struct indirect_report *r) {
    *r=(struct indirect_report){0};
    struct session s={io,r,io->us(io->ctx)};
    r->status=io->status(io->ctx);
    if(r->status==255 || (r->status&3)) { r->result=1; goto done; }
    r->phase=1;
    for(unsigned i=0;i<4;++i) if(!reg_access(&s,0x103b+i,&r->saved[i],0)) goto done;
    if(r->saved[3]!=0 && r->saved[3]!=0x40) { r->result=5; goto done; }
    r->phase=2; r->dirty=1;
    if(!put(&s,0x103e,0x40)) goto cleanup;
    r->phase=3;
    for(unsigned pass=0;pass<2;++pass) {
        static const unsigned addr[]={0,0x80,0xc5e0,0x11ef0};
        static const unsigned len[]={32,32,16,16};
        unsigned pos=0;
        for(unsigned k=0;k<4;++k) for(unsigned j=0;j<len[k];++j) {
            unsigned a=addr[k]+j;
            if(!put(&s,0x103d,a>>16) || !put(&s,0x103c,a>>8) ||
               !put(&s,0x103b,a) || !reg_access(&s,0x103f,&r->sample[pass][pos++],0)) goto cleanup;
            ++r->samples;
        }
    }
cleanup:
    r->failed_phase=r->result?r->phase:0;
    if(r->result) r->failed_address=r->current_address;
    /* A partially consumed host transaction must not be repaired by blind writes. */
    if(r->incomplete) goto done;
    r->phase=4;
    for(unsigned i=0;i<4;++i) if(!put(&s,0x103b+i,r->saved[i])) goto done;
    r->phase=5;
    for(unsigned i=0;i<4;++i) if(!reg_access(&s,0x103b+i,&r->restore_check[i],0)) goto done;
    for(unsigned i=0;i<4;++i) if(r->saved[i]!=r->restore_check[i]) { r->result=6; goto done; }
    r->restored=1; r->dirty=0;
    if(!r->result) for(unsigned i=0;i<INDIRECT_BYTES;++i)
        if(r->sample[0][i]!=r->sample[1][i]) r->result=4;
done:
    if(r->result && !r->failed_phase) {
        r->failed_phase=r->phase; r->failed_address=r->current_address;
    }
    r->elapsed=io->us(io->ctx)-s.start;
}

void ec_chip_info(const struct mapping_io *io,uint8_t values[6],struct indirect_report *r) {
    static const unsigned a[]={0x2000,0x2001,0x2002,0x2085,0x2086,0x2087};
    *r=(struct indirect_report){0};
    struct session s={io,r,io->us(io->ctx)};
    r->status=io->status(io->ctx);
    if(r->status==255 || (r->status&3)) {r->result=1;return;}
    for(unsigned i=0;i<6;++i) if(!reg_access(&s,a[i],&values[i],0)) break;
    r->elapsed=io->us(io->ctx)-s.start;
}

void ec_indirect_page(const struct mapping_io *io,unsigned offset,uint8_t page[256],struct indirect_report *r) {
    *r=(struct indirect_report){0};
    struct session s={io,r,io->us(io->ctx)};
    if((offset&255) || offset>=EC_BACKUP_WINDOW) {r->result=5;goto done;}
    r->phase=1;
    for(unsigned i=0;i<4;++i) if(!reg_access(&s,0x103b+i,&r->saved[i],0)) goto done;
    if(r->saved[3]!=0 && r->saved[3]!=0x40) {r->result=5;goto done;}
    r->phase=2;r->dirty=1;
    if(!put(&s,0x103e,0x40) || !put(&s,0x103d,offset>>16) || !put(&s,0x103c,offset>>8)) goto cleanup;
    r->phase=3;
    for(unsigned i=0;i<256;++i) {
        if(!put(&s,0x103b,i) || !reg_access(&s,0x103f,&page[i],0)) goto cleanup;
        ++r->samples;
    }
cleanup:
    if(r->result) {r->failed_phase=r->phase;r->failed_address=r->current_address;}
    if(r->incomplete) goto done;
    r->phase=4;
    for(unsigned i=0;i<4;++i) if(!put(&s,0x103b+i,r->saved[i])) goto done;
    r->phase=5;
    for(unsigned i=0;i<4;++i) if(!reg_access(&s,0x103b+i,&r->restore_check[i],0)) goto done;
    for(unsigned i=0;i<4;++i) if(r->saved[i]!=r->restore_check[i]) {r->result=6;goto done;}
    r->restored=1;r->dirty=0;
done:
    if(r->result && !r->failed_phase) {r->failed_phase=r->phase;r->failed_address=r->current_address;}
    r->elapsed=io->us(io->ctx)-s.start;
}
