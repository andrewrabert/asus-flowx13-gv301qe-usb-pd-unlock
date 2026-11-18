/* Stock SmiFlash version-only query; no flash enable/erase/program. */
#include <efi.h>
#include <efilib.h>
#include <efipciio.h>
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
 used+=AsciiSPrint(text+used,capacity-used,(const CHAR8 *)"result=%d pci_writes=0 mmio_writes=0 spi_commands=0 ec_access=0\r\n",failed?4:0);
 return used;
}
/* Stock SmiFlash20DC SW-SMI25 reads RBX:RCX as a buffer pointer.
 * A size27, mode0/version0 request takes the short-buffer version response.
 * Mode2 instead changes layout: NEVER select it. No enable20/disable24 calls. */
static void smi_info_request(UINTN buffer) {
    UINTN low=buffer&0xffffffffU,high=buffer>>32,command=0x25;
    __asm__ volatile("outb %%al,%%dx" : "+a"(command), "+b"(low), "+c"(high) : "d"((UINT16)0xb2) : "memory");
}
static void smi_info(CHAR8 *text,UINTN capacity) {
    EFI_PHYSICAL_ADDRESS address=0xffffffffU;
    EFI_STATUS e=uefi_call_wrapper(BS->AllocatePages,4,AllocateMaxAddress,EfiRuntimeServicesData,1,&address);
    if(EFI_ERROR(e)) {AsciiSPrint(text,capacity,(const CHAR8 *)"smi_info_result=1 allocation_status=%lx\r\n",e);return;}
    UINT8 *b=(UINT8 *)(UINTN)address;
    SetMem(b,4096,0);b[0]=0x1b;
    for(unsigned i=0x100;i<4096;++i)b[i]=0xa5;
    smi_info_request((UINTN)address);
    unsigned guard=1;
    for(unsigned i=0x1b;i<0x100;++i)if(b[i])guard=0;
    for(unsigned i=0x100;i<4096;++i)if(b[i]!=0xa5)guard=0;
    unsigned result=(b[0]==0x1b && !b[1] && !b[2] && !b[3] && b[4]==1 && b[5]==0x0e && !b[6] && !b[7] && guard)?0:2;
    UINTN used=AsciiSPrint(text,capacity,(const CHAR8 *)"smi_info_result=%d buffer=%lx guard=%d sw_smi=25 requests=1 enable_disable_requests=0\r\nresponse=",result,address,guard);
    for(unsigned i=0;i<0x1b;++i)used+=AsciiSPrint(text+used,capacity-used,(const CHAR8 *)"%02x",b[i]);
    AsciiSPrint(text+used,capacity-used,(const CHAR8 *)"\r\n");
    uefi_call_wrapper(BS->FreePages,2,address,1);
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
    result=uefi_call_wrapper(root->Open,5,root,&arm,L"\\EFI\\FlowEcAudit\\arm-smi-info.txt",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
    if(EFI_ERROR(result)) { Print(L"SPI probe: not armed. No port access.\r\n"); goto boot; }
    CHAR8 token[64]={0}; UINTN token_size=sizeof(token);
    result=uefi_call_wrapper(arm->Read,3,arm,&token_size,token);
    static const CHAR8 expected[]="GV301QE.415 READ ONLY HOST SPI SMI INFO V1\n";
    int armed=!EFI_ERROR(result) && token_size==sizeof(expected)-1 && CompareMem(token,(VOID *)expected,token_size)==0;
    /* Consume the one-shot token BEFORE issuing any PCI/MMIO read. */
    if(armed) result=uefi_call_wrapper(arm->Delete,1,arm);
    else uefi_call_wrapper(arm->Close,1,arm);
    if(!armed || result!=EFI_SUCCESS) { Print(L"SPI probe: arm token invalid or not consumed.\r\n"); goto boot; }
    result=uefi_call_wrapper(root->Open,5,root,&log,L"\\EFI\\FlowEcAudit\\smi-info.txt",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(EFI_ERROR(result)) goto boot;
    /* Fixed-size log avoids stale bytes from a previous longer report. */
    CHAR8 text[8192]; SetMem(text,sizeof(text),' ');
    static CHAR8 pending[]="Host SPI protection probe started; completion not yet recorded.\r\n";
    CopyMem(text,pending,sizeof(pending)-1);
    if(EFI_ERROR(write_log(log,text,sizeof(text)))) goto close_log;
    SetMem(text,sizeof(text),' ');
    UINTN used=AsciiSPrint(text,sizeof(text),(const CHAR8 *)"Host SPI protection SMI info v1\r\n");
    used+=spi_report(text+used,sizeof(text)-used);
    if(snapshot_ok && !EFI_ERROR(write_log(log,text,sizeof(text)))) smi_info(text+used,sizeof(text)-used);
    write_log(log,text,sizeof(text));
    Print(L"SMI version query complete. Returning to Windows.\r\n");
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
