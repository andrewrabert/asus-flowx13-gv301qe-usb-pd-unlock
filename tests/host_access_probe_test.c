#include <assert.h>
#include <stdio.h>
#include "host_access_probe.h"
static unsigned scenario,writes;
static uint32_t value;
static uint32_t read_reg(unsigned o) {assert(o==0||o==4||o==8);return o==0?value:o==8?0x600:0;}
static void write_byte(uint8_t v) {
 assert(writes<2);assert(v==(writes?0x0c:0xcc));++writes;
 assert(!(v&0x11)); /* never execute or FIFO clear */
 if(scenario==1)return;
 if(scenario==3&&writes==2)return;
 value=(value&~0xff0000U)|((uint32_t)v<<16);
 if(scenario==2&&writes==1)value&=~0x400000U;
}
int main(void) {
 struct host_access_io io={read_reg,write_byte};struct host_access_result r;
 for(scenario=0;scenario<4;++scenario) {
  writes=0;value=0x4f0c2105;host_access_probe(&io,&r);
  assert(writes==2&&r.writes==2);
  assert(r.result==(scenario==0?0:scenario==1?2:scenario==2?3:4));
 }
 writes=0;value=0xffffffff;host_access_probe(&io,&r);assert(r.result==1&&writes==0);
 puts("host access test: success, ignored, partial, restore-failure and refusal passed");
}
