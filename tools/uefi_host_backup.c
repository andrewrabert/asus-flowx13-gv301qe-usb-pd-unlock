/* Read-only host-SPI protection snapshot. No flash transactions. */
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
#define HOST_WINDOW 0x1000000U
static UINT32 host_read32(UINTN address) {
    return *(volatile UINT32 *)address;
}
/* Exact EC header at stock host address; reject before broad mapped reads. */
static int capture_host_window(UINT32 *buffer) {
    static const UINT32 header[]={0xa5a5a5a5,0x94a5a5a5,0x5a5a1285,0x5555bfaa};
    for(unsigned i=0;i<4;++i)
        if(host_read32((UINTN)0xff031080U+i*4)!=header[i]) return 1;
    for(UINTN offset=0;offset<HOST_WINDOW;offset+=4)
        buffer[offset/4]=host_read32((UINTN)0xff000000U+offset);
    return 0;
}
static unsigned host_backup(EFI_FILE_HANDLE root,CHAR8 *text,UINTN capacity) {
    UINT8 *buffer=AllocatePool(2*HOST_WINDOW);
    EFI_FILE_HANDLE files[2]={NULL,NULL};
    CHAR16 *names[]={L"\\EFI\\FlowEcAudit\\host-backup-pass0.bin",L"\\EFI\\FlowEcAudit\\host-backup-pass1.bin"};
    unsigned outcome=0,completed=0;
    if(!buffer) {outcome=10;goto finish;}
    for(unsigned i=0;i<2;++i) {
        EFI_FILE_HANDLE existing=NULL;
        EFI_STATUS e=uefi_call_wrapper(root->Open,5,root,&existing,names[i],EFI_FILE_MODE_READ,0);
        if(!EFI_ERROR(e)) {uefi_call_wrapper(existing->Close,1,existing);outcome=11;goto finish;}
        if(e!=EFI_NOT_FOUND) {outcome=12;goto finish;}
        e=uefi_call_wrapper(root->Open,5,root,&files[i],names[i],EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
        if(EFI_ERROR(e)) {outcome=13;goto finish;}
    }
    for(unsigned pass=0;pass<2;++pass) {
        Print(L"Reading mapped BIOS pass %d/2.\r\n",pass+1);
        if(capture_host_window((UINT32 *)(buffer+pass*HOST_WINDOW))) {outcome=14;goto finish;}
        if(EFI_ERROR(write_log(files[pass],(CHAR8 *)(buffer+pass*HOST_WINDOW),HOST_WINDOW))) {outcome=15;goto finish;}
        ++completed;
    }
    if(CompareMem(buffer,buffer+HOST_WINDOW,HOST_WINDOW)) outcome=16;
finish:
    AsciiSPrint(text,capacity,(const CHAR8 *)"backup_result=%d completed_passes=%d bytes_per_pass=%d hardware_writes=0 explicit_spi_commands=0\r\n",outcome,completed,HOST_WINDOW);
    for(unsigned i=0;i<2;++i) if(files[i]) uefi_call_wrapper(files[i]->Close,1,files[i]);
    if(buffer) FreePool(buffer);
    return outcome;
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
    result=uefi_call_wrapper(root->Open,5,root,&arm,L"\\EFI\\FlowEcAudit\\arm-host-backup.txt",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
    if(EFI_ERROR(result)) { Print(L"SPI probe: not armed. No port access.\r\n"); goto boot; }
    CHAR8 token[64]={0}; UINTN token_size=sizeof(token);
    result=uefi_call_wrapper(arm->Read,3,arm,&token_size,token);
    static const CHAR8 expected[]="GV301QE.415 READ ONLY HOST SPI HOST BACKUP V1\n";
    int armed=!EFI_ERROR(result) && token_size==sizeof(expected)-1 && CompareMem(token,(VOID *)expected,token_size)==0;
    /* Consume the one-shot token BEFORE issuing any PCI/MMIO read. */
    if(armed) result=uefi_call_wrapper(arm->Delete,1,arm);
    else uefi_call_wrapper(arm->Close,1,arm);
    if(!armed || result!=EFI_SUCCESS) { Print(L"SPI probe: arm token invalid or not consumed.\r\n"); goto boot; }
    result=uefi_call_wrapper(root->Open,5,root,&log,L"\\EFI\\FlowEcAudit\\host-backup.txt",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(EFI_ERROR(result)) goto boot;
    /* Fixed-size log avoids stale bytes from a previous longer report. */
    CHAR8 text[8192]; SetMem(text,sizeof(text),' ');
    static CHAR8 pending[]="Host SPI protection probe started; completion not yet recorded.\r\n";
    CopyMem(text,pending,sizeof(pending)-1);
    if(EFI_ERROR(write_log(log,text,sizeof(text)))) goto close_log;
    SetMem(text,sizeof(text),' ');
    UINTN used=AsciiSPrint(text,sizeof(text),(const CHAR8 *)"Host SPI protection host backup v1\r\n");
    used+=spi_report(text+used,sizeof(text)-used);
    if(snapshot_ok) {
        static const UINTN config[]={0xfed80298U,0xfed802a4U,0xfed802b0U};
        for(unsigned i=0;i<3;++i) {
            UINT32 value=host_read32(config[i]);
            used+=AsciiSPrint(text+used,sizeof(text)-used,(const CHAR8 *)"platform_mmio=%08x value=%08x\r\n",(UINT32)config[i],value);
        }
        UINT8 alt=*(volatile UINT8 *)(UINTN)0xfec1001dU;
        used+=AsciiSPrint(text+used,sizeof(text)-used,(const CHAR8 *)"spi_alt_cs=%02x\r\n",alt);
        if(!EFI_ERROR(write_log(log,text,sizeof(text))))
            host_backup(root,text+used,sizeof(text)-used);
    }
    write_log(log,text,sizeof(text));
    Print(L"Host SPI read-only audit complete. Returning to Windows.\r\n");
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
