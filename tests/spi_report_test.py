"""Exercise the actual EFI snapshot routine with fake read-only protocols."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'tools/uefi_spi_probe.c').read_text()
fn=s[s.index('static UINTN spi_report'):s.index('EFI_STATUS efi_main')]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
typedef char CHAR8;
typedef void VOID;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef unsigned long UINTN;
typedef unsigned long EFI_STATUS;
#define EFI_ERROR(x) ((x)!=0)
#define EfiPciIoWidthUint32 2
#define uefi_call_wrapper(f,n,...) (f)(__VA_ARGS__)
typedef struct bridge B;
struct bridge {unsigned SegmentNumber; struct {EFI_STATUS (*Read)(B*,int,UINT64,UINTN,UINT32*);} Pci,Mem;};
typedef B EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL;
int gEfiPciRootBridgeIoProtocolGuid;
static int scenario,pcis,mmios;
static EFI_STATUS pci(B*b,int w,UINT64 a,UINTN n,UINT32*v){
 (void)b;assert(w==2&&n==1);assert((a&~255ULL)==0x140300);
 unsigned o=a&255;assert(o==0||o==8||o==0x40||o==0x44||o==0x48||o==0x50||o==0x54||o==0x58||o==0x5c||o==0xa0||o==0xb8);
 ++pcis;*v=o==0?0x790e1022:o==0xa0?0xfec10000:0;
 if(scenario==2&&o==0)*v=0xffff;
 if(scenario==3&&o==0xa0)*v=0xffffffff;
 if(scenario==4&&o==0x50)return 1;
 return 0;
}
static EFI_STATUS mem(B*b,int w,UINT64 a,UINTN n,UINT32*v){
 (void)b;assert(w==2&&n==1);assert(a==0xfec10000||a==0xfec10004||a==0xfec10008);++mmios;*v=0;return scenario==5?1:0;
}
static B bridge={0,{pci},{mem}};
static EFI_STATUS locate(void*g,void*x,void**b){(void)g;(void)x;*b=&bridge;return scenario==1?1:0;}
static struct {EFI_STATUS(*LocateProtocol)(void*,void*,void**);} services={locate},*BS=&services;
static UINTN AsciiSPrint(char*b,UINTN n,const char*f,...){va_list a;va_start(a,f);int r=vsnprintf(b,n,f,a);va_end(a);assert(r>=0&&(UINTN)r<n);return r;}
'''
suffix=r'''
int main(void){for(scenario=0;scenario<=5;++scenario){char text[8192]={0};pcis=mmios=0;spi_report(text,sizeof(text));
 if(scenario==0){assert(strstr(text,"result=0"));assert(pcis==23&&mmios==6);}
 if(scenario==1){assert(strstr(text,"result=1"));assert(pcis==0&&mmios==0);}
 if(scenario==2){assert(strstr(text,"result=2"));assert(pcis==1&&mmios==0);}
 if(scenario==3||scenario==4){assert(strstr(text,"result=3"));assert(mmios==0);}
 if(scenario==5){assert(strstr(text,"result=3"));assert(mmios==3);}
}puts("SPI snapshot allowlist, identity, BAR and read-failure tests passed");}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(prefix+fn+suffix)
 subprocess.run(['gcc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
