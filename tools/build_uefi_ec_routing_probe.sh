#!/bin/sh
set -eu
efi_tree=${1:?Pass the built GNU-EFI source directory}
dest=out/update_415/uefi_routing_probe
mkdir -p "$dest"
for unit in ec_routing_probe uefi_ec_routing_probe; do
 gcc -I"$efi_tree/inc" -I"$efi_tree/inc/x86_64" -I"$efi_tree/inc/protocol" \
  -DGNU_EFI_USE_MS_ABI -fpic -ffreestanding -fno-stack-protector -fno-stack-check \
  -fshort-wchar -mno-red-zone -maccumulate-outgoing-args -Wall -Wextra -Werror -O2 \
  -c "tools/$unit.c" -o "$dest/$unit.o"
done
ld -nostdlib -znocombreloc -T "$efi_tree/gnuefi/elf_x86_64_efi.lds" -shared -Bsymbolic \
 "$efi_tree/x86_64/gnuefi/crt0-efi-x86_64.o" "$dest/ec_routing_probe.o" "$dest/uefi_ec_routing_probe.o" \
 -L"$efi_tree/x86_64/lib" -L"$efi_tree/x86_64/gnuefi" -lefi -lgnuefi -o "$dest/probe.so"
objcopy -j .text -j .sdata -j .data -j .rodata -j .dynamic -j .dynsym -j .rel -j .rela \
 -j .reloc -O efi-app-x86_64 "$dest/probe.so" "$dest/routing-probe.efi"
