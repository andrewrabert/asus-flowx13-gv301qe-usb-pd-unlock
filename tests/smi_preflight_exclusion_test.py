"""Test the actual EFI critical-section wrapper with simulated control registers."""
from pathlib import Path
import subprocess,tempfile
s=Path('tools/uefi_smi_preflight.c').read_text()
fn=s[s.index('static int inspect_flash(void)'):s.index('static unsigned smi_access(')]
fn=fn.replace('(volatile UINT8 *)(UINTN)0xfed8029bU','&trig').replace('(volatile UINT8 *)(UINTN)0xfed802b2U','&ctrl')
fn=fn.replace('__asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory")','flags=0x200; interrupts=0')
fn=fn.replace('__asm__ volatile("sti":::"memory")','interrupts=1')
fn=fn.replace('exclusion_verified=','if(scenario==1)*trigger &= 0x7f;\n exclusion_verified=')
fn=fn.replace('smi_b2_after=*control;','if(scenario==2)*control ^= 0x40;\n smi_b2_after=*control;')
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "host_spi_writer.h"
typedef uint8_t UINT8;typedef uint64_t UINT64;
static UINT8 trig,ctrl,smi_9b_before,smi_9b_after,smi_b2_before,smi_b2_after;
static unsigned exclusion_verified,scenario,calls,interrupts;
static UINT64 read_elapsed_us;
static const uint8_t host_spi_original_sectors[8192]={0};
static struct host_spi_writer_result flash_read;
static uint8_t host_read8(unsigned o){(void)o;return 0;}
static void host_write8(unsigned o,uint8_t v){(void)o;(void)v;}
static uint64_t now_us(void){return 0;}
void host_spi_writer(const struct host_spi_read_io*i,const uint8_t old[8192],const uint8_t modified[8192],int dry,struct host_spi_writer_result*r){
assert(old==host_spi_original_sectors && !modified && dry==1);
(void)i;++calls;assert(!interrupts);assert(trig==0x9d&&ctrl==0);
r->result=scenario==3?3:scenario==4?2:0;
}
'''
suffix=r'''
int main(void){for(scenario=0;scenario<5;++scenario){trig=0x1d;ctrl=0x40;calls=0;interrupts=1;flash_read.result=99;
int result=inspect_flash();assert(interrupts);assert(calls==(scenario==1?0:1));
assert(result==(scenario==0?0:scenario==1||scenario==4?1:2));assert(trig==0x1d);
if(scenario!=2)assert(ctrl==0x40);
}puts("SPI exclusion wrapper: verified SMI disable before reader, interrupt restoration, refused exclusion and restoration failures passed");}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'t.c').write_text(prefix+fn+suffix)
 subprocess.run(['gcc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Itools',str(p/'t.c'),'-o',str(p/'t')],check=True)
 subprocess.run([str(p/'t')],check=True)
