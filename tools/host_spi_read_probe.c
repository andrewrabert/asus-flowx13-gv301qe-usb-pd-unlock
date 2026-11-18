/* Stock 415 legacy indexed SPI engine. No WREN/status-write/erase/program. */
#include "host_spi_read_probe.h"
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
static int read_command(const struct host_spi_read_io *io,uint8_t opcode,unsigned count,uint8_t *out,unsigned *commands) {
 if(!((opcode==0x9f&&count==3)||(opcode==5&&count==1)))return 0;
 if(!idle(io)||!reset_fifo(io))return 0;
 io->write8(0,opcode);indexed_write(io,5,0);indexed_write(io,6,(uint8_t)count);
 if(!reset_fifo(io))return 0;
 io->write8(2,(uint8_t)((io->read8(2)&~0x10U)|1));++*commands;
 if(!idle(io)||!reset_fifo(io))return 0;
 for(unsigned i=0;i<count;++i)out[i]=io->read8(0x80+i);
 return 1;
}
void host_spi_read_probe(const struct host_spi_read_io *io,struct host_spi_read_result *r) {
 *r=(struct host_spi_read_result){0};
 /* Refuse anything except exact verified unlocked controller state. */
 if(io->read8(2)!=0xcc||io->read8(3)!=0x4f||(io->read8(0x1d)&3)!=0){r->result=1;return;}
 for(unsigned o=4;o<12;++o)if(io->read8(o)){r->result=1;return;}
 uint8_t opcode=io->read8(0),index=io->read8(0x1e);
 uint8_t tx=indexed_read(io,5),rx=indexed_read(io,6),fifo[3];
 for(unsigned i=0;i<3;++i)fifo[i]=io->read8(0x80+i);
 int ok=read_command(io,0x9f,3,r->id,&r->commands);
 if(ok)ok=read_command(io,5,1,&r->status,&r->commands);
 /* Do not write command/count/FIFO state while a transaction is active. */
 if(!idle(io)){r->result=3;return;}
 indexed_write(io,5,tx);indexed_write(io,6,rx);
 for(unsigned i=0;i<3;++i)io->write8(0x80+i,fifo[i]);
 io->write8(0,opcode);
 r->restored=io->read8(0)==opcode && indexed_read(io,5)==tx && indexed_read(io,6)==rx;
 for(unsigned i=0;i<3;++i)if(io->read8(0x80+i)!=fifo[i])r->restored=0;
 io->write8(0x1e,index);
 if(io->read8(0x1e)!=index)r->restored=0;
 r->result=!r->restored?3:!ok?2:0;
}
