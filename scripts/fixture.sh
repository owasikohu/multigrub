#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
efi_inc=$TOOLS/usr/include/efi
efi_lib=$TOOLS/usr/lib
if [[ ! -d $efi_inc ]]; then efi_inc=/usr/include/efi; efi_lib=/usr/lib; fi
mkdir -p "$BUILD/fixture/EFI/BOOT" "$BUILD/fixture/nested/deeper" "$BUILD/fixture/empty"
gcc -I"$efi_inc" -I"$efi_inc/x86_64" -fpic -fshort-wchar -mno-red-zone \
    -fno-stack-protector -DEFI_FUNCTION_WRAPPER -c "$ROOT/tests/fixture.c" -o "$BUILD/fixture.o"
ld -nostdlib -znocombreloc -T "$efi_lib/elf_x86_64_efi.lds" -shared -Bsymbolic \
   "$efi_lib/crt0-efi-x86_64.o" "$BUILD/fixture.o" -L"$efi_lib" -lefi -lgnuefi -o "$BUILD/fixture.so"
objcopy -j .rodata -j .dynstr -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc \
    --target=efi-app-x86_64 "$BUILD/fixture.so" "$BUILD/fixture/EFI/BOOT/BOOTX64.EFI"
printf 'README from ISO\n' > "$BUILD/fixture/README.TXT"
printf 'payload from the ISO\n\0' > "$BUILD/fixture/nested/payload.txt"
printf 'nested bytes\n' > "$BUILD/fixture/nested/deeper/file.txt"
python3 - "$BUILD/fixture/nested/binary.dat" <<'PY'
import sys
open(sys.argv[1], 'wb').write(bytes(range(256)) * 1025)
PY
xorriso -as mkisofs -R -J -V MULTIGRUB -o "$BUILD/test.iso" "$BUILD/fixture"
