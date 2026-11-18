/* Experimental indexed-MMIO core. Hardware programming remains unvalidated. */
#include "host_spi_writer.h"
#include "host_spi_expected_sfdp.h"
struct context { const struct host_spi_read_io *io; struct host_spi_writer_result *r; int unsafe; };
static const uint32_t base[2]={0x3d000,0x42000};
static int equal(const uint8_t *a,const uint8_t *b,unsigned n){for(unsigned i=0;i<n;++i)if(a[i]!=b[i])return 0;return 1;}
static int idle(struct context *c){
 uint64_t t=c->io->time_us();
 for(unsigned i=0;i<100000;++i){
  if(!(c->io->read8(2)&1)&&!(c->io->read8(3)&0x80))return 1;
  if(c->io->time_us()-t>=100000)break;
 }c->unsafe=1;return 0;
}
static uint8_t index_read(struct context *c,uint8_t i){c->io->write8(0x1e,i);return c->io->read8(0x1f);}
static void index_write(struct context *c,uint8_t i,uint8_t v){c->io->write8(0x1e,i);c->io->write8(0x1f,v);}
static int fifo_reset(struct context *c){
 uint64_t t=c->io->time_us();
 for(unsigned i=0;i<100000;++i){
  if(!idle(c))return 0;
  c->io->write8(2,(c->io->read8(2)&~1U)|0x10);
  if(!(c->io->read8(0xd)&7))return 1;
  if(c->io->time_us()-t>=100000)break;
 }return 0;
}
static int in_sector(uint32_t a,unsigned n){
 for(unsigned i=0;i<2;++i)if(a>=base[i]&&a<base[i]+4096&&n<=base[i]+4096-a)return 1;
 return 0;
}
static int command(struct context *c,uint8_t op,uint32_t a,const uint8_t *data,unsigned n,uint8_t *out){
#ifdef HOST_SPI_PREFLIGHT_ONLY
 if(op!=0x9f&&op!=5&&op!=0x35&&op!=0x15&&op!=0x5a&&op!=3)return 0;
#endif
#if defined(HOST_SPI_STOCK_RESTORE_ONLY) || defined(HOST_SPI_STOCK_PROGRAM_ONLY)
 /* Defense in depth: this build can mutate only the first fixed sector. */
 if((op==0x20||op==2)&&!(a>=base[0]&&a<base[0]+4096))return 0;
#endif
#ifdef HOST_SPI_STOCK_PROGRAM_ONLY
 if(op==0x20 || (op==2 && a!=0x3d5e0))return 0;
#endif
 unsigned tx=0,rx=0;
 if((op==0x9f&&n==3)||(op==5&&n==1)||(op==0x35&&n==1)||(op==0x15&&n==1)){if(a)return 0;rx=n;}
 else if(op==0x5a&&n==32&&a<256&&!(a&31)){tx=4;rx=n;}
 else if(op==3&&n==32&&in_sector(a,n)&&!(a&31)){tx=3;rx=n;}
 else if((op==6||op==4)&&n==0&&a==0){}
 else if(op==0x20&&n==0&&(a==base[0]||a==base[1]))tx=3;
 else if(op==2&&n==32&&data&&in_sector(a,n)&&!(a&31)){tx=35;}
 else return 0;
 c->r->failed_opcode=op;c->r->failed_address=a;
 if(!idle(c)||!fifo_reset(c))return 0;
 c->io->write8(0,op);index_write(c,5,(uint8_t)tx);index_write(c,6,(uint8_t)rx);
 if(!fifo_reset(c))return 0;
 if(tx){c->io->write8(0x80,(uint8_t)(a>>16));c->io->write8(0x81,(uint8_t)(a>>8));c->io->write8(0x82,(uint8_t)a);}
 if(op==0x5a)c->io->write8(0x83,0);
 if(op==2)for(unsigned i=0;i<n;++i)c->io->write8(0x83+i,data[i]);
 /* Mark uncertainty BEFORE issuing the erase. A failed completion is not proof
  * that the flash ignored the command. */
 if(op==0x20){c->r->dirty_mask|=a==base[0]?1U:2U;++c->r->erases;}
 if(op==2){c->r->dirty_mask|=a<base[1]?1U:2U;++c->r->programs;}
 c->io->write8(2,(c->io->read8(2)&~0x10U)|1);++c->r->commands;
 if(!idle(c)||!fifo_reset(c))return 0;
 for(unsigned i=0;i<rx;++i)out[i]=c->io->read8(0x80+tx+i);
 return 1;
}
static int status(struct context *c,uint8_t *s){return command(c,5,0,0,1,s);}
static int ready(struct context *c,uint64_t deadline){
 uint64_t t=c->io->time_us();uint8_t s=0xff;
 for(unsigned i=0;i<100000;++i){
  if(!status(c,&s))return 0;
  if(!(s&1))return !(s&2); /* Completed writes must clear WEL. */
  if(c->io->time_us()-t>=deadline)break;
 }c->unsafe=1;return 0; /* Still busy: no reset or controller restoration. */
}
static int enable(struct context *c){uint8_t s;
 return status(c,&s)&&s==0&&command(c,6,0,0,0,0)&&status(c,&s)&&s==2;
}
static int compare(struct context *c,uint32_t a,const uint8_t *expected,unsigned n){
 uint8_t b[32];for(unsigned i=0;i<n;i+=32){if(!command(c,3,a+i,0,32,b))return 0;
  for(unsigned j=0;j<32;++j)if(b[j]!=(expected?expected[i+j]:0xff))return 0;
 }return 1;
}
static int profile(struct context *c){
 uint8_t b[32];
 if(!command(c,0x9f,0,0,3,b)||b[0]!=0xef||b[1]!=0x60||b[2]!=0x18)return 0;
 if(!status(c,b)||b[0]!=0)return 0;
 if(!command(c,0x35,0,0,1,b)||b[0]!=2)return 0;
 if(!command(c,0x15,0,0,1,b)||b[0]!=0x20)return 0;
 for(unsigned a=0;a<256;a+=32)if(!command(c,0x5a,a,0,32,b)||!equal(b,host_spi_expected_sfdp+a,32))return 0;
 return 1;
}
static int patch_ok(const uint8_t *old,const uint8_t *new){
 const unsigned offsets[]={0x5ee,0x5f0,0x5f1,0x1efa,0x1efb};
 const uint8_t before[]={0x4c,0xc0,8,0xd9,0x0b},after[]={0x40,0,9,0x92,0};
 for(unsigned i=0;i<8192;++i){unsigned p=0;while(p<5&&offsets[p]!=i)++p;
  if(p==5){if(old[i]!=new[i])return 0;}else if(old[i]!=before[p]||new[i]!=after[p])return 0;
 }
 const uint8_t guard0[]={0x5a,8,0x64,4},guard1[]={0x5a,8,0x64,9};
 return old[0x5ef]==0&&equal(old+0x5f6,guard0,4)&&equal(old+0x1efe,guard1,4);
}
static void run_writer(const struct host_spi_read_io *io,const uint8_t old[8192],const uint8_t new[8192],int dry_run,int stock_restore,struct host_spi_writer_result *r){
 *r=(struct host_spi_writer_result){.result=1};
 if(!io||!io->read8||!io->write8||!io->time_us||!old||(dry_run!=0&&dry_run!=1))return;
#ifdef HOST_SPI_PREFLIGHT_ONLY
 if(dry_run!=1)return;
#endif
#ifdef HOST_SPI_STOCK_RESTORE_ONLY
 if(!dry_run&&stock_restore!=1)return;
#endif
#ifdef HOST_SPI_STOCK_PROGRAM_ONLY
 if(!dry_run&&stock_restore!=2)return;
#endif
#ifdef HOST_SPI_VID_PATCH_ONLY
 if(!dry_run&&stock_restore)return;
#endif
 if(!dry_run&&!stock_restore&&(!new||!patch_ok(old,new)))return;
 if(stock_restore)new=old; /* This entry has no caller-supplied output image. */
 struct context c={io,r,0};
 if(io->read8(2)!=0xcc||io->read8(3)!=0x4f||(io->read8(0x1d)&3))return;
 for(unsigned i=4;i<12;++i)if(io->read8(i))return;
 uint8_t opcode=io->read8(0),index=io->read8(0x1e),tx=index_read(&c,5),rx=index_read(&c,6),fifo[36];
 for(unsigned i=0;i<36;++i)fifo[i]=io->read8(0x80+i);
 int ok=profile(&c);
 for(unsigned s=0;ok&&s<2;++s)ok=compare(&c,base[s],old+4096*s,4096);
 if(ok&&!dry_run){
  if(stock_restore==2){
   ok=enable(&c)&&command(&c,2,0x3d5e0,old+0x5e0,32,0)&&ready(&c,10000)&&compare(&c,base[0],old,4096);
   if(ok)r->dirty_mask&=~1U;
  }
  for(unsigned s=0;ok&&stock_restore!=2&&s<(stock_restore?1U:2U);++s){
   ok=enable(&c)&&command(&c,0x20,base[s],0,0,0)&&ready(&c,1000000)&&compare(&c,base[s],0,4096);
   for(unsigned a=0;ok&&a<4096;a+=32){
    ok=enable(&c)&&command(&c,2,base[s]+a,new+4096*s+a,32,0)&&ready(&c,10000)&&compare(&c,base[s]+a,new+4096*s+a,32);
   }
   if(ok)ok=compare(&c,base[s],new+4096*s,4096);
   if(ok)r->dirty_mask&=~(1U<<s);
  }
  for(unsigned s=0;ok&&s<2;++s){
   ok=compare(&c,base[s],new+4096*s,4096);
   if(!ok)r->dirty_mask|=1U<<s; /* A later failed read invalidates prior verification. */
  }
 }
 /* Even after a rejected WREN/write, WEL may be set. Only clear it when a
  * verified idle flash and controller permit a new command. Dry run never WREN. */
 uint8_t failed_opcode=r->failed_opcode;
 uint32_t failed_address=r->failed_address;
 uint8_t sr=0xff;
 if(!c.unsafe){if(!status(&c,&sr)||sr&1)c.unsafe=1;
  else if(sr&2){if(dry_run||!command(&c,4,0,0,0,0)||!status(&c,&sr)||sr&3)c.unsafe=1;}
 }
 if(c.unsafe){r->result=3;if(!ok){r->failed_opcode=failed_opcode;r->failed_address=failed_address;}return;}
 if(ok){ok=profile(&c);if(!ok){failed_opcode=r->failed_opcode;failed_address=r->failed_address;}}
 if(c.unsafe){r->result=3;return;}
 index_write(&c,5,tx);index_write(&c,6,rx);
 for(unsigned i=0;i<36;++i)io->write8(0x80+i,fifo[i]);
 io->write8(0,opcode);
 r->restored=io->read8(0)==opcode&&index_read(&c,5)==tx&&index_read(&c,6)==rx;
 for(unsigned i=0;i<36;++i)if(io->read8(0x80+i)!=fifo[i])r->restored=0;
 io->write8(0x1e,index);if(io->read8(0x1e)!=index)r->restored=0;
 r->result=!r->restored?3:ok?0:2;
 if(!ok){r->failed_opcode=failed_opcode;r->failed_address=failed_address;}
 if(ok){r->failed_opcode=0;r->failed_address=0;}
}

void host_spi_writer(const struct host_spi_read_io *io,const uint8_t old[8192],const uint8_t new[8192],int dry_run,struct host_spi_writer_result *r){
 run_writer(io,old,new,dry_run,0,r);
}
void host_spi_stock_restore(const struct host_spi_read_io *io,const uint8_t old[8192],struct host_spi_writer_result *r){
 run_writer(io,old,0,0,1,r);
}

void host_spi_stock_program(const struct host_spi_read_io *io,const uint8_t old[8192],struct host_spi_writer_result *r){
 run_writer(io,old,0,0,2,r);
}
