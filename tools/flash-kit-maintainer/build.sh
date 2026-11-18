#!/bin/sh
# Optional offline rebuild. Does not replace the distributed hardware-tested binaries.
set -eu
efi_tree=${1:?Usage: sh build.sh /absolute/path/to/built/gnu-efi}
cd "$(dirname "$0")"
build_app() {
 name=$1
 define=$2
 shift 2
 dest="build/$name"
 mkdir -p "$dest"
 objects=''
 for unit in "$@"; do
  gcc -I"$efi_tree/inc" -I"$efi_tree/inc/x86_64" -I"$efi_tree/inc/protocol" \
   $define -DGNU_EFI_USE_MS_ABI -fpic -ffreestanding -fno-stack-protector -fno-stack-check \
   -fshort-wchar -mno-red-zone -maccumulate-outgoing-args -Wall -Wextra -Werror -O2 \
   -c "source/$unit.c" -o "$dest/$unit.o"
  objects="$objects $dest/$unit.o"
 done
 ld -nostdlib -znocombreloc -T "$efi_tree/gnuefi/elf_x86_64_efi.lds" -shared -Bsymbolic \
  "$efi_tree/x86_64/gnuefi/crt0-efi-x86_64.o" $objects \
  -L"$efi_tree/x86_64/lib" -L"$efi_tree/x86_64/gnuefi" -lefi -lgnuefi -o "$dest/probe.so"
 objcopy -j .text -j .sdata -j .data -j .rodata -j .dynamic -j .dynsym -j .rel -j .rela \
  -j .reloc -O efi-app-x86_64 "$dest/probe.so" "$dest/$name.efi"
 if cmp -s "$dest/$name.efi" "bin/$name.efi"; then
  echo "$name: byte-identical to tested binary"
 else
  echo "$name: rebuilt successfully; differs from tested binary (toolchain differences possible)"
 fi
}
build_app host-backup '' uefi_host_backup
build_app backup-probe '' ec_indirect_probe uefi_ec_backup_probe
build_app smi-vid-patch -DHOST_SPI_VID_PATCH_ONLY uefi_smi_vid_patch smi_access_probe host_spi_writer
build_app smi-vid-verify -DHOST_SPI_PREFLIGHT_ONLY uefi_smi_vid_verify smi_access_probe host_spi_writer
