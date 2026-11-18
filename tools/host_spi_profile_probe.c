/* Stock 415 legacy indexed SPI engine. No WREN/status-write/erase/program. */
#include "host_spi_profile_probe.h"
static int idle(const struct host_spi_read_io *io) {
 uint64_t start=io->time_us();
 for(unsigned n=0;n<100000;++n) {
  if(!(io->read8(2)&1) && !(io->read8(3)&0x80))return 1;
  if(io->time_us()-start>=100000)return 0;
 }
 return 0;
}
static uint8_t indexed_read(const struct host_spi_read_io *io,uint8_t index) {
 io->write8(0x1e,index);return io->read8(0x1f);
}
static void indexed_write(const struct host_spi_read_io *io,uint8_t index,uint8_t value) {
 io->write8(0x1e,index);io->write8(0x1f,value);
}
static int reset_fifo(const struct host_spi_read_io *io) {
 uint64_t start=io->time_us();
 for(unsigned n=0;n<100000;++n) {
  if(!idle(io))return 0;
  io->write8(2,(uint8_t)((io->read8(2)&~1U)|0x10));
  if(!(io->read8(0x0d)&7))return 1;
  if(io->time_us()-start>=100000)return 0;
 }
 return 0;
}
static int read_command(const struct host_spi_read_io *io,uint8_t opcode,uint32_t address,unsigned count,uint8_t *out,unsigned *commands) {
 unsigned tx=0;
 if((opcode==0x9f&&count==3&&address==0)||((opcode==5||opcode==0x35||opcode==0x15)&&count==1&&address==0))tx=0;
 else if(opcode==0x5a&&count==32&&address<256&&!(address&31))tx=4;
 else if(opcode==3&&count==32&&(address==0x31080||address==0x3d5e0||address==0x42ef0))tx=3;
 else return 0;
 if(!idle(io)||!reset_fifo(io))return 0;
 io->write8(0,opcode);indexed_write(io,5,(uint8_t)tx);indexed_write(io,6,(uint8_t)count);
 if(!reset_fifo(io))return 0;
 if(tx){io->write8(0x80,(uint8_t)(address>>16));io->write8(0x81,(uint8_t)(address>>8));io->write8(0x82,(uint8_t)address);if(tx==4)io->write8(0x83,0);}
 io->write8(2,(uint8_t)((io->read8(2)&~0x10U)|1));++*commands;
 if(!idle(io)||!reset_fifo(io))return 0;
 for(unsigned i=0;i<count;++i)out[i]=io->read8(0x80+tx+i);
 return 1;
}
void host_spi_profile_probe(const struct host_spi_read_io *io,struct host_spi_profile_result *r) {
 *r=(struct host_spi_profile_result){0};
 /* Refuse anything except exact verified unlocked controller state. */
 if(io->read8(2)!=0xcc||io->read8(3)!=0x4f||(io->read8(0x1d)&3)!=0){r->result=1;return;}
 for(unsigned o=4;o<12;++o)if(io->read8(o)){r->result=1;return;}
 uint8_t opcode=io->read8(0),index=io->read8(0x1e);
 uint8_t tx=indexed_read(io,5),rx=indexed_read(io,6),fifo[36];
 for(unsigned i=0;i<36;++i)fifo[i]=io->read8(0x80+i);
 int ok=read_command(io,0x9f,0,3,r->id,&r->commands);
 if(ok)ok=read_command(io,5,0,1,&r->status,&r->commands);
 if(ok && (r->id[0]!=0xef||r->id[1]!=0x60||r->id[2]!=0x18))ok=0;
 if(ok)ok=read_command(io,0x35,0,1,&r->status2,&r->commands);
 if(ok)ok=read_command(io,0x15,0,1,&r->status3,&r->commands);
 for(unsigned pass=0;ok&&pass<2;++pass)for(unsigned off=0;ok&&off<256;off+=32)
  ok=read_command(io,0x5a,off,32,r->sfdp[pass]+off,&r->commands);
 static const uint32_t sample_addresses[]={0x31080,0x3d5e0,0x42ef0};
 for(unsigned i=0;ok&&i<3;++i)ok=read_command(io,3,sample_addresses[i],32,r->samples[i],&r->commands);
 if(ok){for(unsigned i=0;i<256;++i)if(r->sfdp[0][i]!=r->sfdp[1][i])ok=0;
  if(r->sfdp[0][0]!='S'||r->sfdp[0][1]!='F'||r->sfdp[0][2]!='D'||r->sfdp[0][3]!='P')ok=0;}
 /* Do not write command/count/FIFO state while a transaction is active. */
 if(!idle(io)){r->result=3;return;}
 indexed_write(io,5,tx);indexed_write(io,6,rx);
 for(unsigned i=0;i<36;++i)io->write8(0x80+i,fifo[i]);
 io->write8(0,opcode);
 r->restored=io->read8(0)==opcode && indexed_read(io,5)==tx && indexed_read(io,6)==rx;
 for(unsigned i=0;i<36;++i)if(io->read8(0x80+i)!=fifo[i])r->restored=0;
 io->write8(0x1e,index);
 if(io->read8(0x1e)!=index)r->restored=0;
 r->result=!r->restored?3:!ok?2:0;
}
