/* Experimental GV301QE.415 direct register read EC probe. Not a firmware programmer.
 * Hardware use requires separately placing the exact arm token on the ESP.
 * TPL excludes UEFI callbacks, NOT SMM or other bus masters.
 */
#include <efi.h>
#include <efilib.h>
#include "ec_direct_probe.h"
static uint8_t port_in(void *ctx,uint16_t port) {
 uint8_t v;(void)ctx;__asm__ volatile("inb %w1,%0":"=a"(v):"Nd"(port));return v;
}
static void port_out(void *ctx,uint16_t port,uint8_t value) {
 (void)ctx;__asm__ volatile("outb %0,%w1"::"a"(value),"Nd"(port));
}
static int str_equal(const UINT8 *s, const UINT8 *end, const char *expected) {
    while (s<end && *expected && *s==(UINT8)*expected) { ++s; ++expected; }
    return s<end && !*s && !*expected;
}
static int smbios_string(const UINT8 *start,const UINT8 *end,unsigned index,const char *expected) {
    if (!index) return 0;
    while (--index) { while(start<end && *start) ++start; if(start>=end) return 0; ++start; }
    return str_equal(start,end,expected);
}
static int target_matches(void) {
    SMBIOS_TABLE_3_0_ENTRY_POINT *e3=NULL; SMBIOS_TABLE_ENTRY_POINT *e2=NULL;
    UINT8 *table=NULL; UINTN size=0;
    if (!EFI_ERROR(LibGetSystemConfigurationTable(&SMBIOS3TableGuid,(VOID **)&e3))) {
        table=(UINT8 *)(UINTN)e3->TableAddress; size=e3->TableMaximumSize;
    } else if (!EFI_ERROR(LibGetSystemConfigurationTable(&SMBIOSTableGuid,(VOID **)&e2))) {
        table=(UINT8 *)(UINTN)e2->TableAddress; size=e2->TableLength;
    }
    if (!table || size<4 || size>1024*1024) return 0;
    UINTN offset=0; int bios=0,model=0;
    while (offset+4<=size) {
        UINT8 *p=table+offset; UINTN length=p[1], end=offset+length;
        if (length<4 || end>=size) return 0;
        while(end+1<size && (table[end] || table[end+1])) ++end;
        if(end+1>=size) return 0;
        if(p[0]==0 && length>5) bios=smbios_string(p+length,table+end+1,p[5],"GV301QE.415");
        if(p[0]==1 && length>5) model=smbios_string(p+length,table+end+1,p[5],"ROG Flow X13 GV301QE");
        if(p[0]==127) break;
        offset=end+2;
    }
    return bios && model;
}
static EFI_STATUS write_log(EFI_FILE_HANDLE f,CHAR8 *text,UINTN size) {
    EFI_STATUS s=uefi_call_wrapper(f->SetPosition,2,f,0);
    if(EFI_ERROR(s)) return s;
    UINTN written=size;
    s=uefi_call_wrapper(f->Write,3,f,&written,text);
    if(EFI_ERROR(s) || written!=size) return EFI_DEVICE_ERROR;
    return uefi_call_wrapper(f->Flush,1,f);
}
EFI_STATUS efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *system) {
    InitializeLib(image,system);
    EFI_LOADED_IMAGE *loaded=NULL; EFI_FILE_HANDLE root=NULL,arm=NULL,log=NULL;
    EFI_HANDLE next=NULL; EFI_DEVICE_PATH *path=NULL;
    EFI_STATUS result=uefi_call_wrapper(BS->HandleProtocol,3,image,&LoadedImageProtocol,(VOID **)&loaded);
    if(EFI_ERROR(result)) return result;
    root=LibOpenRoot(loaded->DeviceHandle);
    if(!root) return EFI_NOT_FOUND;
    if(!target_matches()) { Print(L"EC probe: model/BIOS guard refused. No port access.\r\n"); goto boot; }
    result=uefi_call_wrapper(root->Open,5,root,&arm,L"\\EFI\\FlowEcAudit\\arm-direct-v3-probe.txt",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
    if(EFI_ERROR(result)) { Print(L"EC probe: not armed. No port access.\r\n"); goto boot; }
    CHAR8 token[64]={0}; UINTN token_size=sizeof(token);
    result=uefi_call_wrapper(arm->Read,3,arm,&token_size,token);
    static const CHAR8 expected[]="GV301QE.415 DIRECT V3 REGISTER READ EC PROBE\n";
    int armed=!EFI_ERROR(result) && token_size==sizeof(expected)-1 && CompareMem(token,(VOID *)expected,token_size)==0;
    /* Consume the one-shot token BEFORE issuing any EC transaction. */
    if(armed) result=uefi_call_wrapper(arm->Delete,1,arm);
    else uefi_call_wrapper(arm->Close,1,arm);
    if(!armed || result!=EFI_SUCCESS) { Print(L"EC probe: arm token invalid or not consumed.\r\n"); goto boot; }
    result=uefi_call_wrapper(root->Open,5,root,&log,L"\\EFI\\FlowEcAudit\\direct-v3-probe.txt",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(EFI_ERROR(result)) goto boot;
    /* Fixed-size log avoids stale bytes from a previous longer report. */
    CHAR8 text[8192]; SetMem(text,sizeof(text),' ');
    static CHAR8 pending[]="EC normal direct probe started; completion not yet recorded.\r\n";
    CopyMem(text,pending,sizeof(pending)-1);
    if(EFI_ERROR(write_log(log,text,sizeof(text)))) goto close_log;
    struct direct_report report;
    const struct direct_io io={NULL,port_in,port_out};
    EFI_TPL previous=uefi_call_wrapper(BS->RaiseTPL,1,TPL_HIGH_LEVEL);
    ec_direct_probe(&io,&report);
    uefi_call_wrapper(BS->RestoreTPL,1,previous);
    SetMem(text,sizeof(text),' ');
    UINTN used=AsciiSPrint(text,sizeof(text),
        (const CHAR8 *)"EC direct probe v3 ports025E/025F\r\nresult=%d initial=%02x host_id=%02x,%02x writes=%d reads=%d restored=%d rows=%d saved=%02x,%02x,%02x\r\n",
        report.result,report.initial,report.host_id[0],report.host_id[1],report.writes,report.reads,
        report.restored,report.rows,report.saved[0],report.saved[1],report.saved[2]);
    for(unsigned i=0;i<report.rows;++i) {
        static const unsigned addresses[DIRECT_COUNT]={0x2000,0x2001,0x2002,0x200a,0x200d,0x1202,0x1063,0x10b0};
        used+=AsciiSPrint(text+used,sizeof(text)-used,
            (const CHAR8 *)"row=%d address=%04x value=%02x\r\n",
            i,addresses[i%DIRECT_COUNT],report.values[i/DIRECT_COUNT][i%DIRECT_COUNT]);
    }
    write_log(log,text,sizeof(text));
    uefi_call_wrapper(log->Close,1,log); log=NULL;
    if(report.result==5) {
        Print(L"EC DIRECT SELECTOR RESTORATION FAILED. CPU halted; physical recovery required.\r\n");
        __asm__ volatile("cli"); for(;;) __asm__ volatile("hlt");
    }
    Print(L"EC direct probe result %d, reads %d. No flash utility entry.\r\n",report.result,report.reads);
close_log:
    if(log) uefi_call_wrapper(log->Close,1,log);
boot:
    uefi_call_wrapper(root->Close,1,root);
    path=FileDevicePath(loaded->DeviceHandle,L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
    if(!path) return EFI_OUT_OF_RESOURCES;
    result=uefi_call_wrapper(BS->LoadImage,6,FALSE,image,path,NULL,0,&next);
    FreePool(path); if(EFI_ERROR(result)) return result;
    result=uefi_call_wrapper(BS->StartImage,3,next,NULL,NULL);
    uefi_call_wrapper(BS->UnloadImage,1,next); return result;
}
