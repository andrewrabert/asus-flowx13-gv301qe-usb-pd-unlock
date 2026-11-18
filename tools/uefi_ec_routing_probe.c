/* Experimental GV301QE.415 normal routing read EC probe. Not a firmware programmer.
 * Hardware use requires separately placing the exact arm token on the ESP.
 * TPL excludes UEFI callbacks, NOT SMM or other bus masters.
 */
#include <efi.h>
#include <efilib.h>
#include <efipciio.h>
#include "ec_routing_probe.h"
static uint64_t ticks_per_us;
static uint64_t ticks(void) {
    uint32_t lo,hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return ((uint64_t)hi<<32)|lo;
}
static uint64_t now_us(void *ctx) { (void)ctx; return ticks()/ticks_per_us; }
static uint8_t port_status(void *ctx) {
    uint8_t v; (void)ctx; __asm__ volatile("inb $0x66,%0":"=a"(v)); return v;
}
static uint8_t port_data(void *ctx) {
    uint8_t v; (void)ctx; __asm__ volatile("inb $0x62,%0":"=a"(v)); return v;
}
static void port_command(void *ctx,uint8_t v) {
    (void)ctx; __asm__ volatile("outb %0,$0x66"::"a"(v));
}
static void port_write_data(void *ctx,uint8_t v) {
    (void)ctx; __asm__ volatile("outb %0,$0x62"::"a"(v));
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
/* Read only PCI configuration via the firmware protocol. No PCI writes. */
static UINTN pci_report(CHAR8 *text,UINTN capacity) {
 EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *bridge=NULL;
 EFI_STATUS status=uefi_call_wrapper(BS->LocateProtocol,3,&gEfiPciRootBridgeIoProtocolGuid,NULL,(VOID **)&bridge);
 if(EFI_ERROR(status)||!bridge||bridge->SegmentNumber!=0)
  return AsciiSPrint(text,capacity,(const CHAR8 *)"pci_root_unavailable=%lx\r\n",status);
 static const unsigned offsets[]={0x00,0x08,0x40,0x44,0x48,0x64,0x74,0x90};
 UINTN used=0;
 for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
  UINT32 value=0xffffffff;
  UINT64 address=((UINT64)20<<16)|((UINT64)3<<8)|offsets[i];
  status=uefi_call_wrapper(bridge->Pci.Read,5,bridge,EfiPciIoWidthUint32,address,1,&value);
  used+=AsciiSPrint(text+used,capacity-used,(const CHAR8 *)"pci=00:14.3 offset=%02x value=%08x status=%lx\r\n",offsets[i],value,status);
 }
 return used;
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
    result=uefi_call_wrapper(root->Open,5,root,&arm,L"\\EFI\\FlowEcAudit\\arm-routing-probe.txt",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
    if(EFI_ERROR(result)) { Print(L"EC probe: not armed. No port access.\r\n"); goto boot; }
    CHAR8 token[64]={0}; UINTN token_size=sizeof(token);
    result=uefi_call_wrapper(arm->Read,3,arm,&token_size,token);
    static const CHAR8 expected[]="GV301QE.415 NORMAL ROUTING READ EC PROBE\n";
    int armed=!EFI_ERROR(result) && token_size==sizeof(expected)-1 && CompareMem(token,(VOID *)expected,token_size)==0;
    /* Consume the one-shot token BEFORE issuing any EC transaction. */
    if(armed) result=uefi_call_wrapper(arm->Delete,1,arm);
    else uefi_call_wrapper(arm->Close,1,arm);
    if(!armed || result!=EFI_SUCCESS) { Print(L"EC probe: arm token invalid or not consumed.\r\n"); goto boot; }
    result=uefi_call_wrapper(root->Open,5,root,&log,L"\\EFI\\FlowEcAudit\\routing-probe.txt",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(EFI_ERROR(result)) goto boot;
    /* Fixed-size log avoids stale bytes from a previous longer report. */
    CHAR8 text[8192]; SetMem(text,sizeof(text),' ');
    static CHAR8 pending[]="EC normal routing probe started; completion not yet recorded.\r\n";
    CopyMem(text,pending,sizeof(pending)-1);
    if(EFI_ERROR(write_log(log,text,sizeof(text)))) goto close_log;
    uint64_t begin=ticks(); uefi_call_wrapper(BS->Stall,1,50000);
    ticks_per_us=(ticks()-begin)/50000;
    if(!ticks_per_us) goto close_log;
    struct routing_report report;
    const struct routing_io io={NULL,port_status,port_data,port_command,port_write_data,now_us};
    EFI_TPL previous=uefi_call_wrapper(BS->RaiseTPL,1,TPL_HIGH_LEVEL);
    ec_routing_probe(&io,&report);
    uefi_call_wrapper(BS->RestoreTPL,1,previous);
    SetMem(text,sizeof(text),' ');
    UINTN used=AsciiSPrint(text,sizeof(text),
        (const CHAR8 *)"EC routing probe v1\r\nresult=%d initial=%02x final=%02x writes=%d reads=%d incomplete=%d rows=%d elapsed_us=%ld\r\n",
        report.result,report.initial,report.final,report.writes,report.reads,
        report.incomplete,report.rows,report.elapsed);
    for(unsigned i=0;i<report.rows;++i) {
        const struct routing_row *r=&report.row[i];
        used+=AsciiSPrint(text+used,sizeof(text)-used,
            (const CHAR8 *)"row=%d address=%04x value=%02x valid=%d phase=%d elapsed_us=%ld status=%02x,%02x,%02x,%02x,%02x\r\n",
            i,r->address,r->value,r->valid,r->phase,r->elapsed,
            r->status[0],r->status[1],r->status[2],r->status[3],r->status[4]);
    }
    if(!report.incomplete) used+=pci_report(text+used,sizeof(text)-used);
    write_log(log,text,sizeof(text));
    uefi_call_wrapper(log->Close,1,log); log=NULL;
    if(report.incomplete) {
        Print(L"EC ROUTING TRANSACTION INCOMPLETE. CPU halted; physical recovery required.\r\n");
        __asm__ volatile("cli"); for(;;) __asm__ volatile("hlt");
    }
    Print(L"EC routing probe result %d, reads %d. No flash utility entry.\r\n",report.result,report.reads);
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
