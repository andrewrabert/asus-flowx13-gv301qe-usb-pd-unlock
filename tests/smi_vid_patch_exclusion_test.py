"""Exercise actual writer critical-section source with faults before/after writes."""
from pathlib import Path
import subprocess
import tempfile
s = Path('tools/uefi_smi_vid_patch.c').read_text()
assert 'ResetSystem' not in s
assert 'host_spi_writer(&io,host_spi_original_sectors,host_spi_vid_sectors,0,&flash_read)' in s
fn = s[s.index('static int inspect_flash(void)'):s.index('static unsigned smi_access(')]
fn = fn.replace('(volatile UINT8 *)(UINTN)0xfed8029bU', '&trig').replace('(volatile UINT8 *)(UINTN)0xfed802b2U', '&ctrl')
fn = fn.replace('__asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory")', 'flags=original_flags; interrupts=0')
fn = fn.replace('__asm__ volatile("sti":::"memory")', 'interrupts=1')
fn = fn.replace('exclusion_verified=', 'if(scenario==1)*trigger &= 0x7f;\n exclusion_verified=')
fn = fn.replace('smi_b2_after=*control;', 'if(scenario==2)*control ^= 0x40;\n smi_b2_after=*control;')
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
#include "host_spi_writer.h"
typedef uint8_t UINT8;typedef uint64_t UINT64;
static UINT8 trig,ctrl,smi_9b_before,smi_9b_after,smi_b2_before,smi_b2_after;
static unsigned exclusion_verified,scenario,calls,interrupts,original_flags,reason;
static UINT64 read_elapsed_us;
static const uint8_t host_spi_original_sectors[8192]={0},host_spi_vid_sectors[8192]={0};
static struct host_spi_writer_result flash_read;
static jmp_buf halt;
static uint8_t host_read8(unsigned o){(void)o;return 0;}
static void host_write8(unsigned o,uint8_t v){(void)o;(void)v;}
static uint64_t now_us(void){return 0;}
static void stop_unsafe(unsigned r){reason=r;assert(!interrupts);longjmp(halt,1);}
void host_spi_writer(const struct host_spi_read_io*i,const uint8_t old[8192],const uint8_t modified[8192],int dry,struct host_spi_writer_result*r){
 assert(old==host_spi_original_sectors && modified==host_spi_vid_sectors && dry==0);(void)i;++calls;
 assert(!interrupts && trig==0x9d && ctrl==0);
 *r=(struct host_spi_writer_result){.result=0,.restored=1,.erases=2,.programs=256};
 if(scenario==3){r->result=3;r->restored=0;}
 if(scenario==4){r->result=2;r->erases=r->programs=0;}
 if(scenario==5){r->result=2;r->dirty_mask=1;}
 if(scenario==6)r->dirty_mask=1; /* Contradictory success still must halt. */
 if(scenario==7)r->restored=0;
 if(scenario==8){r->result=1;r->restored=r->erases=r->programs=0;}
 if(scenario==9){r->result=2;r->erases=1;r->programs=128;} /* Verified first sector but incomplete patch. */
 if(scenario==10)r->erases=1; /* Contradictory success must halt. */
 if(scenario==11)r->programs=255;
 if(scenario==12)r->result=2; /* Final profile failure after completed programming. */
}
'''
suffix = r'''
int main(void){
 for(unsigned flags=0;flags<2;++flags)for(scenario=0;scenario<13;++scenario){
  original_flags=flags?0x200:0;trig=0x1d;ctrl=0x40;calls=reason=0;
  interrupts=flags;flash_read=(struct host_spi_writer_result){.result=99};
  if(setjmp(halt)==0){
   int result=inspect_flash();
   assert(scenario==0||scenario==1||scenario==4||scenario==8);
   assert(result==(scenario==0?0:1));
   assert(interrupts==flags && trig==0x1d && ctrl==0x40);
  }else{
   assert(scenario==2||scenario==3||scenario==5||scenario==6||scenario==7||scenario>=9);
   assert(!interrupts);
   if(scenario==2)assert(reason==2);
   else assert(reason==1 && trig==0x9d && ctrl==0); /* No SMI restoration. */
  }
  assert(calls==(scenario==1?0:1));
 }
 puts("PASS: writer exclusion: unsafe/dirty/unrestored stops before SMI/IF restoration; clean failure/refusal restore; IF initially clear preserved; failed exclusion/restoration, partial patch and final-profile failure covered.");
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td)
 (p/'test.c').write_text(prefix+fn+suffix)
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Itools',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
