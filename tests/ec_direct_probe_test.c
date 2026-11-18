#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../tools/ec_direct_probe.h"
struct mock {unsigned reads,writes,fail;uint8_t index,depth,hi,lo;int absent,wrong_host,wrong_ec,mismatch,restore_failure;unsigned samples;};
static uint8_t input(void *p,uint16_t port){
 struct mock*m=p;uint8_t v=0xff;++m->reads;
 if(m->absent)return 255;
 if(port==0x25e)v=m->index;
 else{assert(port==0x25f);switch(m->index){
 case 0x20:v=m->wrong_host?0x83:0x51;break;case 0x21:v=0x25;break;
 case 0x2e:v=m->depth;break;
 case 0x2f:
  if(m->depth==0x11)v=m->hi;
  else if(m->depth==0x10)v=m->lo;
  else {assert(m->depth==0x12);++m->samples;unsigned addr=(m->hi<<8)|m->lo;
   switch(addr){case 0x2000:v=m->wrong_ec?0:0x51;break;case 0x2001:v=0x25;break;case 0x2002:v=2;break;
   case 0x200a:v=1;break;case 0x200d:v=0x43;break;case 0x1202:v=0;break;case 0x1063:v=1;break;case 0x10b0:v=0;break;default:assert(0);}
   if(m->mismatch && m->samples==16)v=1;
  }break;default:assert(0);
 }}
 if(m->fail==m->reads)v^=0x80;
 return v;
}
static void output(void *p,uint16_t port,uint8_t v){
 struct mock*m=p;++m->writes;
 if(port==0x25e){assert(v==0x20||v==0x21||v==0x2e||v==0x2f||v==7||v==0x87||v==255);m->index=v;return;}
 assert(port==0x25f);
 if(m->index==0x2e){m->depth=v;return;}
 assert(m->index==0x2f);
 /* Every data write must address the address latches, never memory data12. */
 assert(m->depth==0x10||m->depth==0x11);
 if(m->restore_failure && m->samples && v==0x99)return;
 if(m->depth==0x10)m->lo=v;else m->hi=v;
}
static struct mock fresh(void){return (struct mock){.index=7,.depth=0x55,.hi=0x99,.lo=0x88};}
static struct direct_report run(struct mock*m){struct direct_report r;struct direct_io io={m,input,output};ec_direct_probe(&io,&r);return r;}
int main(void){
 struct mock m=fresh();struct direct_report r=run(&m);assert(r.result==0&&r.rows==16&&r.restored);assert(m.index==7&&m.depth==0x55&&m.hi==0x99&&m.lo==0x88);unsigned reads=m.reads;
 m=fresh();m.absent=1;r=run(&m);assert(r.result==2&&m.writes==3&&m.samples==0&&r.restored);
 m=fresh();m.wrong_host=1;r=run(&m);assert(r.result==2&&m.samples==0&&r.restored);
 m=fresh();m.wrong_ec=1;r=run(&m);assert(r.result==3&&m.samples==1&&r.restored);
 m=fresh();m.mismatch=1;r=run(&m);assert(r.result==4&&r.restored);
 m=fresh();m.restore_failure=1;r=run(&m);assert(r.result==5&&!r.restored);
 /* Corrupt each read; verify bounded exit and no memory data writes regardless. */
 for(unsigned n=1;n<=reads;++n){m=fresh();m.fail=n;r=run(&m);assert(m.writes<300&&m.reads<100);}
 printf("direct probe tests passed; %u read fault positions; no memory data writes\n",reads);
}
