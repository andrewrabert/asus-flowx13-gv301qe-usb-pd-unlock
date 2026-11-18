"""Exercise actual version-query wrapper with an emulated documented short reply."""
from pathlib import Path
import subprocess,tempfile
s=Path('tools/uefi_smi_info.c').read_text();fn=s[s.index('static void smi_info(CHAR8'):s.index('EFI_STATUS efi_main')]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
typedef char CHAR8;typedef uint8_t UINT8;typedef unsigned long UINTN,EFI_PHYSICAL_ADDRESS,EFI_STATUS;
#define EFI_ERROR(x) ((x)!=0)
#define AllocateMaxAddress 1
#define EfiRuntimeServicesData 6
#define uefi_call_wrapper(f,n,...) (f)(__VA_ARGS__)
static unsigned scenario,calls,freed;
static UINT8 buffer[4096];
static EFI_STATUS alloc(int type,int mem,UINTN n,EFI_PHYSICAL_ADDRESS*a){assert(type==1&&mem==6&&n==1&&*a==0xffffffff);*a=(UINTN)buffer;return scenario==3?1:0;}
static EFI_STATUS release(EFI_PHYSICAL_ADDRESS a,UINTN n){assert(a==(UINTN)buffer&&n==1);++freed;return 0;}
static struct {EFI_STATUS(*AllocatePages)(int,int,UINTN,EFI_PHYSICAL_ADDRESS*);EFI_STATUS(*FreePages)(EFI_PHYSICAL_ADDRESS,UINTN);} svc={alloc,release},*BS=&svc;
#define SetMem(b,n,v) memset(b,v,n)
static UINTN AsciiSPrint(char*b,UINTN n,const char*f,...){va_list a;va_start(a,f);int r=vsnprintf(b,n,f,a);va_end(a);assert(r>=0&&(UINTN)r<n);return r;}
static void smi_info_request(UINTN addr) {
 assert(addr==(UINTN)buffer);assert(buffer[0]==27);
 for(unsigned i=1;i<256;++i)assert(buffer[i]==0);
 ++calls;if(scenario==1)return;
 buffer[4]=1;buffer[5]=14;if(scenario==2)buffer[256]=0;
}
'''
suffix=r'''
int main(void){for(scenario=0;scenario<4;++scenario){char out[8192];calls=freed=0;smi_info(out,sizeof(out));
assert(calls==(scenario==3?0:1));assert(freed==(scenario==3?0:1));
assert(strstr(out,scenario==0?"smi_info_result=0":scenario==3?"smi_info_result=1":"smi_info_result=2"));
}puts("SMI version wrapper: fixed mode/size, reply, absent handler, guards and allocation failure passed");}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'t.c').write_text(prefix+fn+suffix)
 subprocess.run(['gcc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'t.c'),'-o',str(p/'t')],check=True)
 subprocess.run([str(p/'t')],check=True)
