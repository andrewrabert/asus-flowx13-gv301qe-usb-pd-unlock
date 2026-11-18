#include <assert.h>
#include <stdio.h>
#include "smi_access_probe.h"
static unsigned scenario,count;static uint32_t control,restriction;
static uint32_t read_reg(unsigned o){assert(o==0||o==4||o==8);return o==0?control:o==8?restriction:0;}
static uint8_t request(uint8_t c){assert(c==(count?0x24:0x20));++count;
 if(scenario==1)return 0xa5;
 if(scenario==2&&c==0x20)return 1;
 if(scenario==3)return 0;
 if(scenario==5){control|=0xc00000;return 1;}
 if(c==0x20){control|=0xc00000;restriction=0;}
 else if(scenario!=4){control&=~0xc00000;restriction=0x600;}
 return 0;
}
static unsigned inspected;static int inspect(void){++inspected;assert(count==1);return scenario-6;}
int main(void){struct smi_access_io io={request,read_reg};struct smi_access_result r;
for(scenario=0;scenario<6;++scenario){count=0;control=0x4f0c2105;restriction=0x600;smi_access_probe(&io,&r);
 assert(count==((scenario==1||scenario==2||scenario==5)?1:2));
 assert(r.result==(scenario==0?0:scenario==1||scenario==2?2:scenario==3?3:4));}
for(scenario=6;scenario<9;++scenario){count=inspected=0;control=0x4f0c2105;restriction=0x600;
 smi_access_inspect(&io,&r,inspect);assert(inspected==1);assert(count==(scenario==8?1:2));
 assert(r.result==(scenario==6?0:scenario==7?5:4));}
scenario=0;count=0;control=0x4f0c1096;restriction=0x600;smi_access_probe(&io,&r);assert(!r.result&&r.after==0x4f0c1096&&count==2);
count=0;control=~0U;smi_access_probe(&io,&r);assert(r.result==1&&!count);
puts("SMI access: exact20/24 pairing, ignored/rejected, unchanged, cleanup-failure guards passed");}
