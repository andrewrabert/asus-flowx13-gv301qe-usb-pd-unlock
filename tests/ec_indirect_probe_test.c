#include "../tools/ec_indirect_probe.h"
#include <assert.h>
#include <stdio.h>
struct fake {
    uint64_t time;
    unsigned writes, reads, fault, missing, stage, address, cmd, obf, frozen, mismatch, page_mode, page_offset, chip_mode;
    uint8_t regs[4], initial[4], output;
};
static uint64_t us(void *p) { struct fake *f=p;if(!f->frozen)f->time+=1;return f->time; }
static uint8_t status(void *p) {
    struct fake *f=p;if(f->fault && f->writes==f->fault)return 2;return f->obf;
}
static uint8_t data(void *p) {
    struct fake *f=p;assert(f->obf);f->obf=0;++f->reads;return f->output;
}
static void command(void *p,uint8_t c) {
    struct fake *f=p;assert(!f->stage && !f->obf && (c==0x92 || c==0x93));
    f->cmd=c;f->stage=1;++f->writes;
}
static void write_data(void *p,uint8_t v) {
    struct fake *f=p;++f->writes;
    if(f->stage==1) {f->address=v<<8;f->stage=2;return;}
    if(f->stage==2) {
        f->address|=v;
        if(f->chip_mode) {
            static const unsigned a[]={0x2000,0x2001,0x2002,0x2085,0x2086,0x2087};
            assert(f->reads<6 && f->address==a[f->reads] && f->cmd==0x92);
            f->output=(uint8_t)f->address;
            f->obf=(f->reads+1!=f->missing);f->stage=0;return;
        }
        assert(f->address>=0x103b && f->address<=0x103f);
        if(f->cmd==0x93) {f->stage=3;return;}
        if(f->address==0x103f) {
            assert(f->regs[3]==0x40);
            unsigned a=f->regs[0]|(f->regs[1]<<8)|(f->regs[2]<<16);
            if(f->page_mode) assert(a>=f->page_offset && a<f->page_offset+256);
            else assert(a<32 || (a>=0x80 && a<0xa0) || (a>=0xc5e0 && a<0xc5f0) || (a>=0x11ef0 && a<0x11f00));
            f->output=(uint8_t)(a^(a>>8)^(a>>16));
            if(f->mismatch && f->reads==100) f->output^=1;
        } else f->output=f->regs[f->address-0x103b];
        f->obf=(f->reads+1!=f->missing);f->stage=0;return;
    }
    assert(f->stage==3 && f->cmd==0x93 && f->address<=0x103e);
    if(f->address==0x103e) assert(v==0 || v==0x40);
    f->regs[f->address-0x103b]=v; f->stage=0;
}
static struct indirect_report run(struct fake *f) {
    const struct mapping_io io={f,status,data,command,write_data,us};struct indirect_report r;
    ec_indirect_probe(&io,&r);assert(r.writes==f->writes && r.reads==f->reads);
    assert(r.elapsed<2100000);return r;
}
int main(void) {
    struct fake f={.regs={0x12,0x34,0x01,0}};struct indirect_report r=run(&f);
    assert(!r.result && r.samples==192 && r.restored && !r.dirty);
    assert(f.regs[0]==0x12 && f.regs[1]==0x34 && f.regs[2]==1 && !f.regs[3]);
    unsigned total=f.writes,reads=f.reads;
    for(unsigned i=1;i<=total;++i) {
        f=(struct fake){.fault=i};r=run(&f);assert(r.result && r.incomplete);
        assert(f.writes==i);
    }
    for(unsigned i=1;i<=reads;++i) {
        f=(struct fake){.missing=i};r=run(&f);assert(r.result && r.incomplete);
    }
    f=(struct fake){.regs={0,0,0,0x4f}};r=run(&f);assert(r.result==5 && !r.dirty && f.writes==12);
    f=(struct fake){.mismatch=1};r=run(&f);assert(r.result==4 && r.restored);
    f=(struct fake){.fault=1,.frozen=1};r=run(&f);assert(r.result==2 && r.incomplete);
    printf("PASS: no follow-mode/data-register writes; sample addresses; restore; %u write failures; %u missing responses; selector guard; mismatch; frozen clock\n",total,reads);
    uint8_t page[256];
    for(unsigned off=0;off<EC_BACKUP_WINDOW;off+=256) {
        f=(struct fake){.page_mode=1,.page_offset=off};
        const struct mapping_io io={&f,status,data,command,write_data,us};
        ec_indirect_page(&io,off,page,&r);
        assert(!r.result && r.samples==256 && r.restored);
        for(unsigned i=0;i<256;++i) assert(page[i]==(uint8_t)((off+i)^((off+i)>>8)^((off+i)>>16)));
        total=f.writes;
    }
    for(unsigned k=1;k<=total;++k) {
        f=(struct fake){.page_mode=1,.fault=k};
        const struct mapping_io io={&f,status,data,command,write_data,us};
        ec_indirect_page(&io,0,page,&r);
        assert(r.result && r.incomplete && f.writes==k);
    }
    for(unsigned off=1;off<=EC_BACKUP_WINDOW;off=EC_BACKUP_WINDOW) {
        f=(struct fake){0};const struct mapping_io io={&f,status,data,command,write_data,us};
        ec_indirect_page(&io,off,page,&r);assert(r.result==5 && !f.writes);
        if(off==EC_BACKUP_WINDOW)break;
    }
    puts("PASS: all 768 candidate pages, each byte/address, page write failures, alignment/window guards");
    uint8_t chip[6];
    f=(struct fake){.chip_mode=1};
    const struct mapping_io io={&f,status,data,command,write_data,us};
    ec_chip_info(&io,chip,&r);assert(!r.result && r.reads==6 && r.writes==18);
    for(unsigned i=1;i<=18;++i) {
        f=(struct fake){.chip_mode=1,.fault=i};ec_chip_info(&io,chip,&r);
        assert(r.result==2 && r.incomplete && f.writes==i);
    }
    puts("PASS: chip identification fixed read list and all write timeout positions");
}
