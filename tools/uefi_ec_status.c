/* Boot-path validation only: one status-port read; no EC commands or flash access.
 * Build against GNU-EFI. Writes beside this app's intended installation directory,
 * then starts the existing Windows boot manager from the same EFI partition.
 */
#include <efi.h>
#include <efilib.h>

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system)
{
    EFI_LOADED_IMAGE *loaded = NULL;
    EFI_FILE_HANDLE root = NULL, file = NULL;
    EFI_HANDLE next = NULL;
    EFI_DEVICE_PATH *path = NULL;
    EFI_STATUS result, log_result;
    UINTN size;
    UINT8 status;
    CHAR8 report[] = "Flow X13 UEFI status probe v1\r\nEC port 0x66 = 0x00\r\nPort writes: 0\r\nFlash mode: not entered\r\n";
    CHAR8 hex[] = "0123456789abcdef";
    UINTN i;
    InitializeLib(image, system);
    result = uefi_call_wrapper(BS->HandleProtocol, 3, image, &LoadedImageProtocol, (VOID **)&loaded);
    if (EFI_ERROR(result)) return result;
    root = LibOpenRoot(loaded->DeviceHandle);
    if (root == NULL) return EFI_NOT_FOUND;
    __asm__ volatile ("inb %1, %0" : "=a"(status) : "Nd"((UINT16)0x66));
    for (i = 0; i + 3 < sizeof(report); ++i) {
        if (report[i] == '=' && report[i+2] == '0' && report[i+3] == 'x') {
            report[i+4] = hex[status >> 4]; report[i+5] = hex[status & 15];
            break;
        }
    }
    log_result = uefi_call_wrapper(root->Open, 5, root, &file,
        L"\\EFI\\FlowEcAudit\\status.txt",
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (!EFI_ERROR(log_result)) {
        size = sizeof(report) - 1;
        log_result = uefi_call_wrapper(file->Write, 3, file, &size, report);
        if (!EFI_ERROR(log_result) && size != sizeof(report)-1) log_result = EFI_DEVICE_ERROR;
        uefi_call_wrapper(file->Flush, 1, file);
        uefi_call_wrapper(file->Close, 1, file);
    }
    uefi_call_wrapper(root->Close, 1, root);
    Print(L"EC status %02x; log: %r. Starting Windows.\r\n", status, log_result);
    path = FileDevicePath(loaded->DeviceHandle, L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
    if (path == NULL) return EFI_OUT_OF_RESOURCES;
    result = uefi_call_wrapper(BS->LoadImage, 6, FALSE, image, path, NULL, 0, &next);
    FreePool(path);
    if (EFI_ERROR(result)) return result;
    result = uefi_call_wrapper(BS->StartImage, 3, next, NULL, NULL);
    uefi_call_wrapper(BS->UnloadImage, 1, next);
    return result;
}
