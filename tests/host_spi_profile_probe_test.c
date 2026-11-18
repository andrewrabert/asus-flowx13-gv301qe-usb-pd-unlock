#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "host_spi_profile_probe.h"
static uint8_t regs[256],idx[256],before[256],sfdp[256];static unsigned scenario,writes,commands;static uint64_t ticks;
static uint8_t read8(unsigned o){assert(o<256);return o==0x1f?idx[regs[0x1e]]:regs[o];}
static void write8(unsigned o,uint8_t v){++writes;
 assert(o==0||o==2||o==0x1e||o==0x1f||(o>=0x80&&o<0xa4));
 if(o==0x1e){assert(v==5||v==6||v==0x13);regs[o]=v;return;}
 if(o==0x1f){assert(regs[0x1e]==5||regs[0x1e]==6);idx[regs[0x1e]]=v;return;}
 if(o==2){assert((v&~0x11)==0xcc);assert((v&0x11)!=0x11);
  if(v&1){++commands;uint8_t op=regs[0],tx=idx[5],rx=idx[6];
   if(scenario==2){regs[2]=v;return;}
   if(op==0x9f){assert(tx==0&&rx==3);regs[0x80]=scenario==5?0xc2:0xef;regs[0x81]=0x60;regs[0x82]=0x18;}
   else if(op==5||op==0x35||op==0x15){assert(tx==0&&rx==1);regs[0x80]=op==5?0:op==0x35?2:0x60;}
   else if(op==0x5a){assert(tx==4&&rx==32);assert(!regs[0x80]&&!regs[0x81]&&!regs[0x83]);unsigned a=regs[0x82];assert(!(a&31)&&a<=224);memcpy(regs+0x84,sfdp+a,32);if(scenario==6&&commands==13)regs[0x84]^=1;}
   else {assert(op==3&&tx==3&&rx==32);unsigned a=((unsigned)regs[0x80]<<16)|((unsigned)regs[0x81]<<8)|regs[0x82];assert(a==0x31080||a==0x3d5e0||a==0x42ef0);for(unsigned i=0;i<32;++i)regs[0x83+i]=(uint8_t)(a+i);}
  }
  regs[2]=0xcc;regs[0xd]=scenario==3||scenario==4?7:0;return;
 }
 if(o==0)assert(v==0x9f||v==5||v==0x35||v==0x15||v==0x5a||v==3||v==before[0]);
 regs[o]=v;
}
static uint64_t time_us(void){if(scenario==4)return 0;return ticks+=10;}
int main(void){struct host_spi_read_io io={read8,write8,time_us};struct host_spi_profile_result r;
for(unsigned i=0;i<256;++i)sfdp[i]=(uint8_t)i;
memcpy(sfdp,"SFDP",4);
for(scenario=0;scenario<8;++scenario){memset(regs,0,sizeof regs);memset(idx,0,sizeof idx);writes=commands=0;ticks=0;
 regs[0]=0x96;regs[1]=0x10;regs[2]=0xcc;regs[3]=0x4f;regs[0x1e]=0x13;idx[5]=3;idx[6]=4;
 for(unsigned i=0;i<36;++i)regs[0x80+i]=(uint8_t)(0xa5^i);
 if(scenario==1)regs[2]=0x0c;
 if(scenario==7)regs[0x1d]=1;
 memcpy(before,regs,sizeof regs);host_spi_profile_probe(&io,&r);
 if(scenario==0){assert(!r.result&&r.restored&&r.commands==23&&commands==23);assert(!memcmp(r.sfdp[0],sfdp,256)&&!memcmp(r.sfdp[1],sfdp,256));assert(r.status==0&&r.status2==2&&r.status3==0x60);assert(r.samples[0][0]==0x80&&r.samples[1][0]==0xe0&&r.samples[2][0]==0xf0);}
 if(scenario==1||scenario==7)assert(r.result==1&&!writes&&!commands);
 if(scenario==2)assert(r.result==3&&!r.restored&&commands==1);
 if(scenario==3||scenario==4)assert(r.result==2&&r.restored&&!commands);
 if(scenario==5)assert(r.result==2&&r.restored&&commands==2);
 if(scenario==6)assert(r.result==2&&r.restored&&commands==23);
 if(r.restored){assert(regs[0]==before[0]&&regs[0x1e]==before[0x1e]);assert(idx[5]==3&&idx[6]==4);assert(!memcmp(regs+0x80,before+0x80,36));}
}puts("SPI profile: fixed read opcodes/addresses/counts, two-pass SFDP, wrong chip, wrong CS, timeouts and restoration passed");}
