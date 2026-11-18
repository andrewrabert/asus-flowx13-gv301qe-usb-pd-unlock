#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "host_spi_writer.h"
#include "host_spi_expected_sfdp.h"
static uint8_t regs[256],idx[256],old[8192],new[8192],flash[8192],saved[256];
static unsigned commands,writes,wel,busy,erases,programs,fail_command,scenario;
static uint64_t ticks;
static unsigned address_offset(unsigned a,unsigned n){
 if(a>=0x3d000&&a+n<=0x3e000)return a-0x3d000;
 assert(a>=0x42000&&a+n<=0x43000);return a-0x42000+4096;
}
static uint8_t read8(unsigned o){assert(o<256);return o==0x1f?idx[regs[0x1e]]:regs[o];}
static void write8(unsigned o,uint8_t v){
 ++writes;
 assert(o==0||o==2||o==0x1e||o==0x1f||(o>=0x80&&o<0xa4));
 if(o==0x1e){assert(v==5||v==6||v==0x13);regs[o]=v;return;}
 if(o==0x1f){assert(regs[0x1e]==5||regs[0x1e]==6);idx[regs[0x1e]]=v;return;}
 if(o!=2){regs[o]=v;return;}
 assert((v&~0x11)==0xcc);assert((v&0x11)!=0x11);
 if(v&1){
  ++commands;
  if(commands==fail_command){regs[2]=v;return;}
  unsigned tx=idx[5],rx=idx[6],a=((unsigned)regs[0x80]<<16)|((unsigned)regs[0x81]<<8)|regs[0x82];
  uint8_t op=regs[0];
  assert(!busy||op==5);
  if(op==0x9f){assert(tx==0&&rx==3);regs[0x80]=(scenario==1||(scenario==11&&programs==256))?0xc2:0xef;regs[0x81]=0x60;regs[0x82]=0x18;}
  else if(op==5){assert(!tx&&rx==1);regs[0x80]=(uint8_t)(wel*2+busy);if(busy&&scenario!=5)busy=0;}
  else if(op==0x35||op==0x15){assert(!tx&&rx==1);regs[0x80]=op==0x35?2:(scenario==2?0x24:0x20);}
  else if(op==0x5a){assert(tx==4&&rx==32&&a<=224&&!(a&31)&&!regs[0x83]);memcpy(regs+0x84,host_spi_expected_sfdp+a,32);if(scenario==3)regs[0x84]^=1;}
  else if(op==3){assert(tx==3&&rx==32);memcpy(regs+0x83,flash+address_offset(a,32),32);if(scenario==10&&programs==256&&a==0x3d000)regs[0x83]^=1;}
  else if(op==6){assert(!tx&&!rx);if(scenario!=4)wel=1;}
  else if(op==4){assert(!tx&&!rx);wel=0;}
  else if(op==0x20){assert(tx==3&&!rx&&wel&&(a==0x3d000||a==0x42000));++erases;
   memset(flash+address_offset(a,4096),255,4096);wel=0;busy=1;
   if(scenario==6)flash[address_offset(a,4096)+200]=0;
  }
  else if(op==2){assert(tx==35&&!rx&&wel&&!(a&31));++programs;
   unsigned off=address_offset(a,32);assert((a&255)+32<=256);
   for(unsigned i=0;i<32;++i)flash[off+i]&=regs[0x83+i];
   wel=0;busy=1;if(scenario==7)flash[off]^=1;
  }
  else assert(!"opcode outside exact allowlist");
 }
 regs[2]=0xcc;regs[0xd]=scenario==8?7:0;
}
static uint64_t time_us(void){return scenario==9?0:(ticks+=100);}
static void init(unsigned s,unsigned fail){
 memset(regs,0,sizeof regs);memset(idx,0,sizeof idx);memcpy(flash,old,8192);
 scenario=s;fail_command=fail;commands=writes=wel=busy=erases=programs=0;ticks=0;
 regs[0]=0x96;regs[1]=0x10;regs[2]=0xcc;regs[3]=0x4f;regs[0x1e]=0x13;idx[5]=3;idx[6]=4;
 for(unsigned i=0;i<36;++i)regs[0x80+i]=(uint8_t)(0xa5^i);
 memcpy(saved,regs,256);
}
static void restored(void){assert(!memcmp(regs,saved,256));assert(idx[5]==3&&idx[6]==4);}
static void load(const char *path,uint8_t *p){FILE *f=fopen(path,"rb");assert(f);assert(fread(p,1,4096,f)==4096);assert(fgetc(f)==EOF);fclose(f);}
int main(void){
 load("out/update_415/host_sector_bundle_v1/03d000.original.bin",old);
 load("out/update_415/host_sector_bundle_v1/042000.original.bin",old+4096);
 load("out/update_415/host_sector_bundle_v1/03d000.modified.UNAPPROVED.bin",new);
 load("out/update_415/host_sector_bundle_v1/042000.modified.UNAPPROVED.bin",new+4096);
 const struct host_spi_read_io io={read8,write8,time_us};struct host_spi_writer_result r;
 init(0,0);host_spi_writer(&io,old,NULL,1,&r);
 assert(r.result==0&&r.restored&&!erases&&!programs&&!wel&&!r.dirty_mask);restored();
 unsigned dry_commands=commands;
 init(0,0);memcpy(flash,new,8192);host_spi_writer(&io,new,NULL,1,&r);
 assert(r.result==0&&r.restored&&!erases&&!programs);restored();
 init(0,0);host_spi_writer(&io,new,NULL,1,&r);
 assert(r.result==2&&r.restored&&!erases&&!programs);restored();

#if !defined(HOST_SPI_PREFLIGHT_ONLY) && !defined(HOST_SPI_STOCK_RESTORE_ONLY) && !defined(HOST_SPI_VID_PATCH_ONLY)
 init(0,0);host_spi_stock_program(&io,old,&r);
 assert(r.result==0&&r.restored&&!erases&&programs==1&&!wel&&!r.dirty_mask);
 assert(!memcmp(flash,old,8192));restored();unsigned same_commands=commands;
 for(unsigned fail=1;fail<=same_commands;++fail){
  init(0,fail);host_spi_stock_program(&io,old,&r);
  assert(r.result==3&&!r.restored&&commands==fail&&!erases&&programs<=1);
 }
 for(unsigned s=1;s<=8;++s){if(s==6)continue;
  init(s,0);host_spi_stock_program(&io,old,&r);
  assert(r.result!=0&&!erases&&programs<=1);
  if(s==5)assert(r.result==3&&r.dirty_mask==1&&!r.restored);
  if(s==7)assert(r.dirty_mask==1&&programs==1);
 }
 init(0,0);flash[4096]^=1;host_spi_stock_program(&io,old,&r);assert(r.result==2&&!erases&&!programs);restored();
 init(9,1);host_spi_stock_program(&io,old,&r);assert(r.result==3&&commands==1);
 printf("PASS: same-byte program: %u commands and all stuck positions, zero erases, one 32-byte program, full comparisons and failure guards.\n",same_commands);
#else
 init(0,0);host_spi_stock_program(&io,old,&r);assert(r.result==1&&!writes&&!commands);
#endif
#ifdef HOST_SPI_STOCK_PROGRAM_ONLY
 init(0,0);host_spi_writer(&io,old,new,0,&r);assert(r.result==1&&!writes&&!commands);
 init(0,0);host_spi_stock_restore(&io,old,&r);assert(r.result==1&&!writes&&!commands);
 printf("PASS: program-only build refuses modified image and erase/restore before MMIO; preflight %u commands.\n",dry_commands);
 return 0;
#endif
#if !defined(HOST_SPI_PREFLIGHT_ONLY) && !defined(HOST_SPI_VID_PATCH_ONLY)
 init(0,0);host_spi_stock_restore(&io,old,&r);
 assert(r.result==0&&r.restored&&erases==1&&programs==128&&!wel&&!r.dirty_mask);
 assert(!memcmp(flash,old,8192));restored();unsigned stock_commands=commands;
 for(unsigned fail=1;fail<=stock_commands;++fail){
  init(0,fail);host_spi_stock_restore(&io,old,&r);
  assert(r.result==3&&!r.restored&&commands==fail&&erases<=1&&programs<=128);
  assert(!memcmp(flash+4096,old+4096,4096));
 }
 for(unsigned s=1;s<=8;++s){init(s,0);host_spi_stock_restore(&io,old,&r);
  assert(r.result!=0&&erases<=1&&programs<=128);
  assert(!memcmp(flash+4096,old+4096,4096));
  if(s<=4||s==8)assert(!erases&&!programs);
  if(s==5)assert(r.result==3&&r.dirty_mask==1&&!r.restored&&!programs);
  if(s==6)assert(r.dirty_mask==1&&!programs);
  if(s==7)assert(r.dirty_mask==1&&programs==1);
 }
 init(0,0);flash[4096]^=1;host_spi_stock_restore(&io,old,&r);assert(r.result==2&&!erases&&!programs);restored();
 init(9,1);host_spi_stock_restore(&io,old,&r);assert(r.result==3&&commands==1);
 printf("PASS: stock-sector erase/restore, %u commands and all stuck positions, second sector unchanged, failure and stale-sector guards.\n",stock_commands);
#else
 init(0,0);host_spi_stock_restore(&io,old,&r);assert(r.result==1&&!writes&&!commands);
#endif
#if defined(HOST_SPI_PREFLIGHT_ONLY) || defined(HOST_SPI_STOCK_RESTORE_ONLY)
 init(0,0);host_spi_writer(&io,old,new,0,&r);assert(r.result==1&&!writes&&!commands);
 for(unsigned fail=1;fail<=dry_commands;++fail){init(0,fail);host_spi_writer(&io,old,NULL,1,&r);assert(r.result==3&&commands==fail&&!erases&&!programs&&!wel);}
 for(unsigned s=1;s<=3;++s){init(s,0);host_spi_writer(&io,old,NULL,1,&r);assert(r.result==2&&r.restored&&!erases&&!programs&&!wel);}
 init(0,0);flash[4096]^=1;host_spi_writer(&io,old,NULL,1,&r);assert(r.result==2&&!erases&&!programs);restored();
 printf("PASS: compile-time read-only preflight, %u commands, all stuck-engine positions, full-sector mismatch, wrong ID/protection/SFDP, write mode refused before MMIO.\n",dry_commands);
#else
 init(0,0);host_spi_writer(&io,old,new,0,&r);
 assert(r.result==0&&r.restored&&erases==2&&programs==256&&!wel&&!r.dirty_mask);
 assert(!memcmp(flash,new,8192));restored();unsigned total=commands;
 for(unsigned s=1;s<=8;++s){init(s,0);host_spi_writer(&io,old,new,0,&r);
  assert(r.result!=0);
  if(s<=4||s==8)assert(!erases&&!programs);
  if(s==5)assert(r.result==3&&r.dirty_mask==1&&!r.restored&&!programs);
  if(s==6)assert(r.dirty_mask==1&&!programs);
  if(s==7)assert(r.dirty_mask==1&&programs==1);
  if(r.restored)restored();
 }
 init(0,0);flash[4096]^=1;host_spi_writer(&io,old,new,0,&r);assert(r.result==2&&!erases&&!programs);restored();
 init(0,0);new[0]^=1;host_spi_writer(&io,old,new,0,&r);assert(r.result==1&&!writes);new[0]^=1;
 for(unsigned fail=1;fail<=total;++fail){init(0,fail);host_spi_writer(&io,old,new,0,&r);
  assert(r.result==3&&!r.restored&&commands==fail); /* No follow-up transaction after stuck engine. */
 }
 init(9,1);host_spi_writer(&io,old,new,0,&r);assert(r.result==3&&commands==1);
 init(10,0);host_spi_writer(&io,old,new,0,&r);assert(r.result==2&&r.dirty_mask==1&&r.failed_opcode==3&&r.failed_address==0x3d000);
 init(11,0);host_spi_writer(&io,old,new,0,&r);assert(r.result==2&&r.failed_opcode==0x9f);
 printf("PASS: actual MMIO core dry-run %u commands; write %u commands; all %u stuck-command positions; frozen clock; wrong ID/protection/SFDP; refused WREN; permanent busy; failed erase; bad program; stale sector; patch guard.\n",dry_commands,total,total);
#endif
}
