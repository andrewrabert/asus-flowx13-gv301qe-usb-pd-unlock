/* Paired stock SMI enable/disable; caller must verify no MAC restore list first.
 * No code/data flash reads or programming transactions are requested here. */
#include "smi_access_probe.h"
void smi_access_inspect(const struct smi_access_io *io,struct smi_access_result *r,int (*inspect)(void)) {
 *r=(struct smi_access_result){0};r->before=io->read(0);
 if((r->before&0xffff0000U)!=0x4f0c0000U || io->read(4)!=0 || io->read(8)!=0x600U){r->result=1;return;}
 r->enable_status=io->request(0x20);++r->requests;
 if(r->enable_status) {
  /* Rejection should precede callbacks. Verify that assumption before returning. */
  r->after=io->read(0);r->restriction_after=io->read(8);
  r->result=(r->after==r->before && r->restriction_after==0x600U && io->read(4)==0)?2:4;
  return;
 }
 r->during=io->read(0);r->restriction_during=io->read(8);
 int inspected=0;
 if(inspect && r->during==(r->before|0xc00000U) && r->restriction_during==0 && io->read(4)==0) {
  inspected=inspect();
  /* An active/stuck controller must be cold-reset, not reconfigured by SMM. */
  if(inspected==2){r->result=4;return;}
 }
 /* Otherwise accepted enable ALWAYS paired with disable. */
 r->disable_status=io->request(0x24);++r->requests;
 r->after=io->read(0);r->restriction_after=io->read(8);
 if(r->disable_status || r->after!=r->before || r->restriction_after!=0x600U || io->read(4)!=0){r->result=4;return;}
 if(r->during!=(r->before|0xc00000U) || r->restriction_during!=0)r->result=3;
 else if(inspected)r->result=5;
}

void smi_access_probe(const struct smi_access_io *io,struct smi_access_result *r){smi_access_inspect(io,r,0);}
