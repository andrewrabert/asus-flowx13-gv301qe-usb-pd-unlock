"""Exercise the actual EFI wrapper's refusals and restoration with mocked I/O."""
from pathlib import Path
import subprocess,tempfile
s=Path('tools/uefi_smi_access.c').read_text()
fn=s[s.index('static unsigned smi_access(CHAR8'):s.index('EFI_STATUS efi_main')]
fn=fn.replace('*(volatile UINT8 *)(UINTN)0xfed80286U','status86').replace('*(volatile UINT8 *)(UINTN)0xfed802acU','ac')
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>
#include "smi_access_probe.h"
typedef char CHAR8;typedef uint8_t UINT8;typedef uint16_t UINT16;typedef uint32_t UINT32;
typedef unsigned long UINTN,EFI_PHYSICAL_ADDRESS,EFI_STATUS;
typedef struct {uint32_t a;uint16_t b,c;uint8_t d[8];} EFI_GUID;
#define EFI_NOT_FOUND 14
#define EFI_ERROR(x) ((x)!=0)
#define AllocateMaxAddress 1
#define EfiRuntimeServicesData 6
#define uefi_call_wrapper(f,n,...) (f)(__VA_ARGS__)
static unsigned scenario,calls,freed,buffer_guard_failed,restores;
static UINT8 buffer[4096],*smi_buffer,status86,ac,pic;
static UINT16 pm;static UINT32 control,restriction;
static EFI_STATUS getvar(const wchar_t*n,EFI_GUID*g,UINT32*a,UINTN*z,UINT8*d){assert(!wcscmp(n,L"MACNamesListVar"));assert(g->a==0xf70ebfe2&&g->d[7]==0x9d);(void)a;(void)z;(void)d;return scenario==1?0:EFI_NOT_FOUND;}
static EFI_STATUS alloc(int t,int m,UINTN n,EFI_PHYSICAL_ADDRESS*a){assert(t==1&&m==6&&n==1&&*a==0xffffffff);*a=(UINTN)buffer;return scenario==2?1:0;}
static EFI_STATUS release(EFI_PHYSICAL_ADDRESS a,UINTN n){assert(a==(UINTN)buffer&&n==1);++freed;return 0;}
static struct {EFI_STATUS(*AllocatePages)(int,int,UINTN,EFI_PHYSICAL_ADDRESS*);EFI_STATUS(*FreePages)(EFI_PHYSICAL_ADDRESS,UINTN);} bs={alloc,release},*BS=&bs;
static struct {EFI_STATUS(*GetVariable)(const wchar_t*,EFI_GUID*,UINT32*,UINTN*,UINT8*);} rt={getvar},*RT=&rt;
static UINTN AsciiSPrint(char*b,UINTN n,const char*f,...){va_list a;va_start(a,f);int r=vsnprintf(b,n,f,a);va_end(a);assert(r>=0&&(UINTN)r<n);return r;}
static UINT16 read_pm_enable(void){return pm;}static UINT8 read_pic(void){return pic;}
static void write_pm_enable(UINT16 v){++restores;if(scenario!=7)pm=v;}
static UINT32 spi_control_read(unsigned o){return o==0?control:o==8?restriction:0;}
static UINT8 smi_access_request(uint8_t cmd){assert(cmd==(calls?0x24:0x20));++calls;
 if(scenario==4)return 0xa5;
 if(scenario==8){ac^=1;return 1;}
 if(cmd==0x20){control|=0xc00000;restriction=0;pm=0;}
 else {control=0x4f0c2105;restriction=0x600;pm=0x120;if(scenario==5)pic^=2;if(scenario==6)buffer_guard_failed=1;}
 return 0;
}
'''
suffix=r'''
int main(void){for(scenario=0;scenario<9;++scenario){char out[8192];calls=freed=restores=0;pm=0x123;pic=0xa0;ac=0xc0;control=0x4f0c2105;restriction=0x600;status86=scenario==3?8:0;
unsigned reset=smi_access(out,sizeof(out));assert(reset==(scenario>=5));
assert(calls==(scenario>=1&&scenario<=3?0:scenario==4||scenario==8?1:2));
assert(freed==(scenario==1||scenario==2?0:1));
assert(restores==(scenario==0||scenario==5||scenario==6||scenario==7?1:0));
if(scenario==0)assert(pm==0x123&&strstr(out,"smi_access_result=0"));
}puts("SMI wrapper: variable/allocation/status refusal, PM restoration, rejected request, PIC/buffer/restore failures passed");}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'t.c').write_text(prefix+fn+suffix)
 subprocess.run(['gcc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Itools',str(p/'t.c'),'tools/smi_access_probe.c','-o',str(p/'t')],check=True)
 subprocess.run([str(p/'t')],check=True)
