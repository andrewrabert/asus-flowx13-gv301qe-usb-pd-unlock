#include "../tools/ec_control_probe.h"
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
        static const unsigned addresses[16]={0x019f,0x1040,0x1041,0x1042,0x1043,0x1044,0x1063,0x10b0,0x2030,0x2000,0x2001,0x2002,0x103b,0x103c,0x103d,0x103e};
        assert(f->address==addresses[k]);
        f->obf=(f->reads+1!=f->missing);
    }
}
static uint8_t data(void *p) {
    struct fake *f=p; assert(f->stage==3 && f->obf); f->stage=0;f->obf=0;
    unsigned effective=f->address<0x1000?0x80000+f->address:0xf00000+f->address;
    ++f->reads;
    return (uint8_t)((effective>>8)^effective^(f->mismatch && f->reads==17));
}
static struct control_report run(struct fake *f) {
    const struct control_io io={f,status,data,command,write_data,us};
    struct control_report r;ec_control_probe(&io,&r);
    assert(r.writes==f->writes && r.reads==f->reads && r.elapsed<600000);
    return r;
}
int main(void) {
    struct fake f={0};struct control_report r=run(&f);
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
