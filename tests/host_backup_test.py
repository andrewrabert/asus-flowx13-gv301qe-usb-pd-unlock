"""Exercise actual mapped-reader bounds and header rejection without hardware."""
from pathlib import Path
import subprocess,tempfile
s=Path('tools/uefi_host_backup.c').read_text()
fn=s[s.index('static int capture_host_window'):s.index('static unsigned host_backup')]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
typedef uintptr_t UINTN;
typedef uint32_t UINT32;
#define HOST_WINDOW 0x1000000U
static unsigned calls,bad;
static UINT32 host_read32(UINTN a) {
 assert(a>=0xff000000ULL && a<=0xfffffffcULL && !(a&3));
 ++calls;
 static const UINT32 h[]={0xa5a5a5a5,0x94a5a5a5,0x5a5a1285,0x5555bfaa};
 if(a>=0xff031080 && a<0xff031090)return h[(a-0xff031080)/4]^(bad?1:0);
 return (UINT32)a;
}
'''
suffix=r'''
int main(void) {
 UINT32*b=malloc(HOST_WINDOW);assert(b);
 assert(capture_host_window(b)==0);assert(calls==4+HOST_WINDOW/4);
 assert(b[0]==0xff000000U && b[HOST_WINDOW/4-1]==0xfffffffcU);
 calls=0;bad=1;assert(capture_host_window(b)==1);assert(calls==1);
 free(b);puts("host mapped read: exact bounds and header refusal passed");
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'t.c').write_text(prefix+fn+suffix)
 subprocess.run(['gcc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'t.c'),'-o',str(p/'t')],check=True)
 subprocess.run([str(p/'t')],check=True)
