/* Non-programming test: WREN remains restricted throughout. */
#include "host_access_probe.h"
void host_access_probe(const struct host_access_io *io,struct host_access_result *r) {
    *r=(struct host_access_result){0};
    r->before=io->read(0);r->restricted=io->read(8);
    /* Exact previously observed idle state: ExecuteOpCode and FifoPtrClr are 0.
       No SPI opcode/FIFO/restricted-command register is ever written. */
    if(r->before!=0x4f0c2105U || r->restricted!=0x600U || io->read(4)!=0) {
        r->result=1;return;
    }
    io->write_control_byte(0xcc);++r->writes;
    r->during=io->read(0);
    /* Always restore, including when the enable attempt was ignored/partial. */
    io->write_control_byte(0x0c);++r->writes;
    r->after=io->read(0);
    if(r->after!=r->before || io->read(8)!=0x600U || io->read(4)!=0) {
        r->result=4;return;
    }
    if(r->during==r->before) r->result=2; /* set request ignored */
    else if(r->during!=(r->before|0xc00000U)) r->result=3;
}
