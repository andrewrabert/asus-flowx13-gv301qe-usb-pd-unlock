#include "../tools/ec_mapping_probe.h"
#include <assert.h>
#include <stdio.h>
struct fake {
    uint64_t t;
    unsigned writes,reads,stage,address,obf,fault,missing,initial,frozen,mismatch,early;
};
static uint64_t us(void *p) { struct fake *f=p; if(!f->frozen) f->t+=10; return f->t; }
static uint8_t status(void *p) {
    struct fake *f=p;
    if(!f->writes && f->initial) return f->initial;
    if(f->fault && f->writes==f->fault) return 2;
    if(f->early && f->writes==f->early) return 1;
    return f->obf;
}
static void command(void *p,uint8_t c) {
    struct fake *f=p; assert(c==0x92 && f->stage==0 && !f->obf);
    ++f->writes;f->stage=1;
}
static void write_data(void *p,uint8_t v) {
    struct fake *f=p; assert(f->stage==1 || f->stage==2); ++f->writes;
    if(f->stage++==1) f->address=v<<8;
    else {
        f->address|=v;
        unsigned k=f->reads%16;
        assert(f->address==(k?0x1040+k-1:0x019f));
        f->obf=(f->reads+1!=f->missing);
    }
}
static uint8_t data(void *p) {
    struct fake *f=p; assert(f->stage==3 && f->obf); f->stage=0;f->obf=0;
    unsigned effective=f->address<0x1000?0x80000+f->address:0xf00000+f->address;
    ++f->reads;
    return (uint8_t)((effective>>8)^effective^(f->mismatch && f->reads==17));
}
static struct mapping_report run(struct fake *f) {
    const struct mapping_io io={f,status,data,command,write_data,us};
    struct mapping_report r;ec_mapping_probe(&io,&r);
    assert(r.writes==f->writes && r.reads==f->reads && r.elapsed<600000);
    return r;
}
int main(void) {
    struct fake f={0};struct mapping_report r=run(&f);
    assert(!r.result && r.reads==32 && r.writes==96 && !r.incomplete);
    for(unsigned k=1;k<=96;++k) {
        f=(struct fake){.fault=k};r=run(&f);
        assert(r.result==2 && r.incomplete && r.writes==k);
    }
    for(unsigned k=1;k<=32;++k) {
        f=(struct fake){.missing=k};r=run(&f);
        assert(r.result==2 && r.incomplete && r.reads==k-1);
    }
    for(unsigned k=1;k<=2;++k) {
        f=(struct fake){.early=k};r=run(&f);
        assert(r.result==3 && r.incomplete && !r.reads);
    }
    for(unsigned k=1;k<256;++k) if(k==255 || (k&3)) {
        f=(struct fake){.initial=k};r=run(&f);assert(r.result==1 && !r.writes);
    }
    f=(struct fake){.mismatch=1};r=run(&f);assert(r.result==4 && !r.incomplete);
    f=(struct fake){.fault=1,.frozen=1};r=run(&f);assert(r.result==2 && r.incomplete);
    puts("PASS: fixed 92 reads, byte order/address translation, 96 write failures, 32 missing responses, unexpected OBF, busy/absent, mismatch, frozen clock");
}
