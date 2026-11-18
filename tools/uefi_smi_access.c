/* Paired stock SMM flash-access test; no erase/program commands. */
#include <efi.h>
#include <efilib.h>
#include <efipciio.h>
#include "smi_access_probe.h"
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

/* Fixed read allowlist. No PCI writes, MMIO writes, SPI commands or EC access. */
/* Called only after exact model, BIOS, PCI identity and BAR guards.
 * These are control/restricted-command registers, not FIFO or read-to-clear data.
 * Stock FlashDriver uses this same host physical mapping. */
static UINT32 spi_control_read(unsigned offset) {
    return *(volatile UINT32 *)(UINTN)(0xfec10000U+offset);
}
static int snapshot_ok;
static UINTN spi_report(CHAR8 *text,UINTN capacity) {
 EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *bridge=NULL;
 EFI_STATUS status=uefi_call_wrapper(BS->LocateProtocol,3,&gEfiPciRootBridgeIoProtocolGuid,NULL,(VOID **)&bridge);
 if(EFI_ERROR(status)||!bridge||bridge->SegmentNumber!=0)
  return AsciiSPrint(text,capacity,(const CHAR8 *)"result=1 pci_root_unavailable=%lx\r\n",status);
 UINT32 id=0;
 UINT64 address=((UINT64)20<<16)|((UINT64)3<<8);
 status=uefi_call_wrapper(bridge->Pci.Read,5,bridge,EfiPciIoWidthUint32,address,1,&id);
 if(EFI_ERROR(status)||id!=0x790e1022)
  return AsciiSPrint(text,capacity,(const CHAR8 *)"result=2 wrong_bridge=%08x status=%lx mmio_reads=0\r\n",id,status);
 static const unsigned offsets[]={0x00,0x08,0x40,0x44,0x48,0x50,0x54,0x58,0x5c,0xa0,0xb8};
 UINTN used=0; UINT32 bar=0; int failed=0;
 for(unsigned pass=0;pass<2;++pass) {
  for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
   UINT32 value=0xffffffff;
   status=uefi_call_wrapper(bridge->Pci.Read,5,bridge,EfiPciIoWidthUint32,address|offsets[i],1,&value);
   used+=AsciiSPrint(text+used,capacity-used,(const CHAR8 *)"pass=%d pci=00:14.3 offset=%02x value=%08x status=%lx\r\n",pass,offsets[i],value,status);
   if(EFI_ERROR(status)) failed=1;
   if(offsets[i]==0xa0) bar=value;
  }
  /* Stock BIOS fixes this controller base. Refuse any other mapping. */
  if(failed || (bar&0xffffffe0U)!=0xfec10000U) {
   used+=AsciiSPrint(text+used,capacity-used,(const CHAR8 *)"result=3 mmio_refused bar=%08x\r\n",bar);
   return used;
  }
  /* Control0 and restricted-command registers only; no FIFO/data window. */
  for(unsigned offset=0;offset<=8;offset+=4) {
   UINT32 value=0xffffffff;
   value=spi_control_read(offset);
   status=EFI_SUCCESS;
   used+=AsciiSPrint(text+used,capacity-used,(const CHAR8 *)"pass=%d mmio=%08x value=%08x status=%lx\r\n",pass,0xfec10000+offset,value,status);
   if(EFI_ERROR(status)) failed=1;
  }
 }
 snapshot_ok=!failed;
 used+=AsciiSPrint(text+used,capacity-used,(const CHAR8 *)"snapshot_result=%d snapshot_pci_writes=0 snapshot_mmio_writes=0 snapshot_spi_commands=0 snapshot_ec_access=0\r\n",failed?4:0);
 return used;
}

static UINT8 *smi_buffer;
static unsigned buffer_guard_failed;
static UINT8 smi_access_request(uint8_t command) {
 SetMem(smi_buffer,4096,0);SetMem(smi_buffer+256,4096-256,0xa5);smi_buffer[16]=0xa5;
 UINTN low=(UINTN)smi_buffer&0xffffffffU,high=(UINTN)smi_buffer>>32,cmd=command;
 __asm__ volatile("outb %%al,%%dx" : "+a"(cmd), "+b"(low), "+c"(high) : "d"((UINT16)0xb2) : "memory");
 for(UINTN i=256;i<4096;++i)if(smi_buffer[i]!=0xa5)buffer_guard_failed=1;
 return smi_buffer[16];
}
static UINT16 read_pm_enable(void){UINT16 v;__asm__ volatile("inw %%dx,%%ax":"=a"(v):"d"((UINT16)0x802));return v;}
static UINT8 read_pic(void){UINT8 v;__asm__ volatile("inb $0x21,%0":"=a"(v));return v;}
static void write_pm_enable(UINT16 v){__asm__ volatile("outw %%ax,%%dx"::"a"(v),"d"((UINT16)0x802));}
static unsigned smi_access(CHAR8 *text,UINTN capacity) {
 static EFI_GUID guid={0xf70ebfe2,0x7bad,0x4683,{0x8d,0xaf,0x00,0xad,0x21,0x25,0x06,0x9d}};
 UINT8 data=0;UINTN size=1;UINT32 attributes=0;
 EFI_STATUS e=uefi_call_wrapper(RT->GetVariable,5,L"MACNamesListVar",&guid,&attributes,&size,&data);
 if(e!=EFI_NOT_FOUND){AsciiSPrint(text,capacity,(const CHAR8 *)"smi_access_refused=mac_restore_list status=%lx\r\n",e);return 0;}
 EFI_PHYSICAL_ADDRESS addr=0xffffffffU;
 e=uefi_call_wrapper(BS->AllocatePages,4,AllocateMaxAddress,EfiRuntimeServicesData,1,&addr);
 if(EFI_ERROR(e)){AsciiSPrint(text,capacity,(const CHAR8 *)"smi_access_refused=allocation status=%lx\r\n",e);return 0;}
 smi_buffer=(UINT8 *)(UINTN)addr;
 /* Stock disable loops on this status until it clears. Refuse an already asserted source. */
 if((*(volatile UINT8 *)(UINTN)0xfed80286U & 8)!=0){
  AsciiSPrint(text,capacity,(const CHAR8 *)"smi_access_refused=pending_status_86_bit3\r\n");
  uefi_call_wrapper(BS->FreePages,2,addr,1);smi_buffer=NULL;return 0;
 }
 buffer_guard_failed=0;
 UINT16 pm_before=read_pm_enable();UINT8 pic_before=read_pic();
 UINT8 ac_before=*(volatile UINT8 *)(UINTN)0xfed802acU;
 struct smi_access_result r;const struct smi_access_io io={smi_access_request,spi_control_read};
 smi_access_probe(&io,&r);
 UINT16 pm_after=read_pm_enable();UINT8 pic_after=read_pic();
 UINT8 ac_after=*(volatile UINT8 *)(UINTN)0xfed802acU;
 /* Stock disable forces PM1 enable=120 rather than saving it; restore our snapshot. */
 if(r.requests==2 && pm_after!=pm_before)write_pm_enable(pm_before);
 UINT16 pm_restored=read_pm_enable();
 unsigned cold_reset=buffer_guard_failed || r.result==4 || (r.requests!=0 && (pm_restored!=pm_before || pic_after!=pic_before || ac_after!=ac_before));
 AsciiSPrint(text,capacity,(const CHAR8 *)"smi_access_result=%d requests=%d enable_status=%02x disable_status=%02x before=%08x during=%08x after=%08x restricted_during=%08x restricted_after=%08x pm_before=%04x pm_after=%04x pm_restored=%04x pic_before=%02x pic_after=%02x ac_before=%02x ac_after=%02x cold_reset=%d buffer_guard_failed=%d mac_restore_list_absent=1 explicit_erase_program_commands=0\r\n",r.result,r.requests,r.enable_status,r.disable_status,r.before,r.during,r.after,r.restriction_during,r.restriction_after,pm_before,pm_after,pm_restored,pic_before,pic_after,ac_before,ac_after,cold_reset,buffer_guard_failed);
 uefi_call_wrapper(BS->FreePages,2,addr,1);smi_buffer=NULL;
 return cold_reset;
}
EFI_STATUS efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *system) {
    InitializeLib(image,system);
    EFI_LOADED_IMAGE *loaded=NULL; EFI_FILE_HANDLE root=NULL,arm=NULL,log=NULL;
    EFI_HANDLE next=NULL; EFI_DEVICE_PATH *path=NULL;
    EFI_STATUS result=uefi_call_wrapper(BS->HandleProtocol,3,image,&LoadedImageProtocol,(VOID **)&loaded);
    if(EFI_ERROR(result)) return result;
    root=LibOpenRoot(loaded->DeviceHandle);
    if(!root) return EFI_NOT_FOUND;
    if(!target_matches()) { Print(L"SPI probe: model/BIOS guard refused. No port access.\r\n"); goto boot; }
    result=uefi_call_wrapper(root->Open,5,root,&arm,L"\\EFI\\FlowEcAudit\\arm-smi-access.txt",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
    if(EFI_ERROR(result)) { Print(L"SPI probe: not armed. No port access.\r\n"); goto boot; }
    CHAR8 token[64]={0}; UINTN token_size=sizeof(token);
    result=uefi_call_wrapper(arm->Read,3,arm,&token_size,token);
    static const CHAR8 expected[]="GV301QE.415 PAIRED SMM ACCESS TEST V1\n";
    int armed=!EFI_ERROR(result) && token_size==sizeof(expected)-1 && CompareMem(token,(VOID *)expected,token_size)==0;
    /* Consume the one-shot token BEFORE issuing any PCI/MMIO read. */
    if(armed) result=uefi_call_wrapper(arm->Delete,1,arm);
    else uefi_call_wrapper(arm->Close,1,arm);
    if(!armed || result!=EFI_SUCCESS) { Print(L"SPI probe: arm token invalid or not consumed.\r\n"); goto boot; }
    result=uefi_call_wrapper(root->Open,5,root,&log,L"\\EFI\\FlowEcAudit\\smi-access.txt",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(EFI_ERROR(result)) goto boot;
    /* Fixed-size log avoids stale bytes from a previous longer report. */
    CHAR8 text[8192]; SetMem(text,sizeof(text),' ');
    static CHAR8 pending[]="Host SPI protection probe started; completion not yet recorded.\r\n";
    CopyMem(text,pending,sizeof(pending)-1);
    if(EFI_ERROR(write_log(log,text,sizeof(text)))) goto close_log;
    SetMem(text,sizeof(text),' ');
    UINTN used=AsciiSPrint(text,sizeof(text),(const CHAR8 *)"Host SPI protection SMI access v1\r\n");
    used+=spi_report(text+used,sizeof(text)-used);
    unsigned cold_reset=0;
    if(snapshot_ok && !EFI_ERROR(write_log(log,text,sizeof(text)))) cold_reset=smi_access(text+used,sizeof(text)-used);
    if(cold_reset) {
        write_log(log,text,sizeof(text));uefi_call_wrapper(log->Close,1,log);log=NULL;
        Print(L"SMI cleanup unconfirmed. Cold reboot to restore firmware defaults.\r\n");
        uefi_call_wrapper(RT->ResetSystem,4,EfiResetCold,EFI_SUCCESS,0,NULL);return EFI_DEVICE_ERROR;
    }
    write_log(log,text,sizeof(text));
    Print(L"Paired SMI access test complete. Returning to Windows.\r\n");
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
