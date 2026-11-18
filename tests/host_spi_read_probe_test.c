#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "host_spi_read_probe.h"
static uint8_t regs[256],idx[256],before[256];static unsigned scenario,writes,commands;static uint64_t ticks;
static uint8_t read8(unsigned o){assert(o<256);return o==0x1f?idx[regs[0x1e]]:regs[o];}
static void write8(unsigned o,uint8_t v){++writes;
 assert(o==0||o==2||o==0x1e||o==0x1f||(o>=0x80&&o<=0x82));
 if(o==0x1e){assert(v==5||v==6||v==0x13);regs[o]=v;return;}
 if(o==0x1f){assert(regs[0x1e]==5||regs[0x1e]==6);idx[regs[0x1e]]=v;return;}
 if(o==2){assert((v&~0x11)==0xcc);assert((v&0x11)!=0x11);
  if(v&1){++commands;assert(idx[5]==0);assert(regs[0]==0x9f||regs[0]==5);
   assert(idx[6]==(regs[0]==0x9f?3:1));
   if(scenario==2){regs[2]=v;return;}
   if(regs[0]==0x9f){regs[0x80]=0xef;regs[0x81]=0x40;regs[0x82]=0x18;}else regs[0x80]=0;
  }
  regs[2]=0xcc;regs[0x0d]=scenario==3||scenario==5?7:0;return;
 }
 if(o==0)assert(v==0x9f||v==5||v==before[0]);
 regs[o]=v;
}
static uint64_t time_us(void){if(scenario==4||scenario==5)return 0;return ticks+=10;}
int main(void){struct host_spi_read_io io={read8,write8,time_us};struct host_spi_read_result r;
for(scenario=0;scenario<8;++scenario){memset(regs,0,sizeof regs);memset(idx,0,sizeof idx);writes=commands=0;ticks=0;
 regs[0]=scenario==6?0x96:5;regs[1]=0x21;regs[2]=0xcc;regs[3]=0x4f;regs[0x1e]=0x13;idx[5]=3;idx[6]=4;
 regs[0x80]=0x12;regs[0x81]=0x34;regs[0x82]=0x56;
 if(scenario==1)regs[2]=0x0c;
 if(scenario==7)regs[0x1d]=1;
 if(scenario==4)regs[0x0d]=7;
 memcpy(before,regs,sizeof regs);
 host_spi_read_probe(&io,&r);
 if(scenario==0||scenario==4||scenario==6){assert(r.result==0&&r.restored&&r.commands==2&&commands==2);assert(!memcmp(r.id,"\xef\x40\x18",3)&&r.status==0);}
 if(scenario==1||scenario==7)assert(r.result==1&&!writes&&!commands);
 if(scenario==2)assert(r.result==3&&!r.restored&&commands==1);
 if(scenario==3||scenario==5)assert(r.result==2&&r.restored&&!commands);
 if(r.restored){assert(regs[0]==before[0]&&regs[0x1e]==before[0x1e]);assert(idx[5]==3&&idx[6]==4);assert(!memcmp(regs+0x80,before+0x80,3));}
}
puts("Host SPI read: opcode allowlist, exact counts, refusal, engine timeout, FIFO timeout and restoration passed");}
