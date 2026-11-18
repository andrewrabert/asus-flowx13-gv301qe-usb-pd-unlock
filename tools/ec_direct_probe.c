#include "ec_direct_probe.h"
struct session { const struct direct_io *io; struct direct_report *r; };
static void out(struct session*s,uint16_t p,uint8_t v){s->io->out(s->io->ctx,p,v);++s->r->writes;}
static uint8_t in(struct session*s,uint16_t p){++s->r->reads;return s->io->in(s->io->ctx,p);}
static uint8_t config_read(struct session*s,uint8_t index){out(s,0x25e,index);return in(s,0x25f);}
static void select_depth(struct session*s,uint8_t index){out(s,0x25e,0x2e);out(s,0x25f,index);}
static uint8_t depth_read(struct session*s,uint8_t index){select_depth(s,index);return config_read(s,0x2f);}
static void address_write(struct session*s,uint8_t index,uint8_t value){
 /* Private helper called ONLY for address latches10/11, never data12. */
 select_depth(s,index);out(s,0x25e,0x2f);out(s,0x25f,value);
}
static uint8_t memory_read(struct session*s,uint16_t address){
 address_write(s,0x11,address>>8);address_write(s,0x10,address&255);return depth_read(s,0x12);
}
void ec_direct_probe(const struct direct_io *io,struct direct_report *r){
 *r=(struct direct_report){0};struct session s={io,r};
 r->initial=in(&s,0x25e);
 /* FF may be a saved index, so test read-only host IDs before concluding absent. */
 r->host_id[0]=config_read(&s,0x20);r->host_id[1]=config_read(&s,0x21);
 if(r->host_id[0]!=0x51 || r->host_id[1]!=0x25){r->result=2;goto restore_index;}
 r->saved[0]=config_read(&s,0x2e);
 r->saved[1]=depth_read(&s,0x11);r->saved[2]=depth_read(&s,0x10);
 for(unsigned pass=0;pass<2;++pass)for(unsigned k=0;k<DIRECT_COUNT;++k){
  static const uint16_t addresses[DIRECT_COUNT]={0x2000,0x2001,0x2002,0x200a,0x200d,0x1202,0x1063,0x10b0};
  uint8_t v=memory_read(&s,addresses[k]);r->values[pass][k]=v;++r->rows;
  static const uint8_t id[3]={0x51,0x25,0x02};
  if(k<3 && v!=id[k]){r->result=3;goto restore_depth;}
  if(pass && v!=r->values[0][k]){r->result=4;goto restore_depth;}
 }
restore_depth:
 address_write(&s,0x11,r->saved[1]);address_write(&s,0x10,r->saved[2]);
 if(depth_read(&s,0x11)!=r->saved[1] || depth_read(&s,0x10)!=r->saved[2])r->result=5;
 select_depth(&s,r->saved[0]);
 if(config_read(&s,0x2e)!=r->saved[0])r->result=5;
restore_index:
 out(&s,0x25e,r->initial);
 r->restored=in(&s,0x25e)==r->initial && r->result!=5;
 if(!r->restored)r->result=5;
}
