#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
"$ROOT/scripts/build.sh"
"$ROOT/scripts/fixture.sh" > "$BUILD/compat-fixture.log" 2>&1
out=$BUILD/compatibility
mkdir -p "$out"
case_test() {
  local name=$1 code=$2
  cat > "$out/boot.cfg" <<CFG
serial --unit=0 --speed=115200
terminal_input serial
terminal_output console serial
insmod hello
insmod chain
insmod boot
insmod gcry_sha256
search --label DATA --set=root
if bootiso_boot /iso/$name.iso; then
 echo 'UNEXPECTED SUCCESS'
else
 echo 'EXPECTED FAILURE'
fi
halt
CFG
  python3 "$ROOT/scripts/image.py" --config "$out/boot.cfg" --iso "$out/$name.iso" --name compatibility
  printf 'preserve existing scratch\n' > "$out/sentinel"
  mcopy -i "$out/disk.img@@1141899264" "$out/sentinel" ::/sentinel
  python3 "$ROOT/scripts/qemu.py" "$out/disk.img" --expect "$code"
  cp "$out/serial.log" "$out/$name.log"
  if [[ $code == NO_EFI_LOADER || $code == FILE_TOO_LARGE || $code == SYMLINK_UNSUPPORTED ]]; then
    mtype -i "$out/disk.img@@1141899264" ::/sentinel | cmp - "$out/sentinel"
  fi
}
rm -rf "$out/tree"
mkdir -p "$out/tree"
printf 'no EFI\n' > "$out/tree/readme"
xorriso -as mkisofs -R -o "$out/missing.iso" "$out/tree" > "$out/missing-build.log" 2>&1
case_test missing NO_EFI_LOADER
for kind in file directory dangling cycle continuation; do
  rm -rf "$out/tree"
  cp -a "$BUILD/fixture" "$out/tree"
  case "$kind" in
    file) ln -s README.TXT "$out/tree/link";;
    directory) ln -s nested "$out/tree/link";;
    dangling) ln -s absent "$out/tree/link";;
    cycle) ln -s link "$out/tree/link";;
    continuation) ln -s "$(python3 -c 'print("a/"*150+"target")')" "$out/tree/$(python3 -c 'print("l"*240)')";;
  esac
  xorriso -as mkisofs -R -o "$out/link-$kind.iso" "$out/tree" > "$out/link-$kind-build.log" 2>&1
  case_test "link-$kind" SYMLINK_UNSUPPORTED
done
# Small synthetic ISO with deliberately declared multi-extent size > FAT limit.
# No 4GiB content is read: rejection must happen in preflight.
rm -rf "$out/tree"
cp -a "$BUILD/fixture" "$out/tree"
printf 'small' > "$out/tree/LARGE.BIN"
xorriso -as mkisofs -o "$out/large.iso" "$out/tree" > "$out/large-build.log" 2>&1
python3 - "$out/large.iso" <<'PY'
import pathlib,struct,sys
p=pathlib.Path(sys.argv[1]);b=bytearray(p.read_bytes())
block=struct.unpack_from('<I',b,16*2048+158)[0];size=struct.unpack_from('<I',b,16*2048+166)[0]
pos=block*2048;end=pos+size
while pos<end:
    n=b[pos]
    if not n:pos=(pos//2048+1)*2048;continue
    name=b[pos+33:pos+33+b[pos+32]]
    if name.startswith(b'LARGE.BIN'):
        rec=bytearray(b[pos:pos+n]);rec[25]|=128
        struct.pack_into('<I',rec,10,0xffffffff);struct.pack_into('>I',rec,14,0xffffffff)
        last=bytearray(rec);last[25]&=127
        struct.pack_into('<I',last,10,1);struct.pack_into('>I',last,14,1)
        stop=((pos//2048)+1)*2048
        assert not any(b[stop-n:stop]),'need padding for extra extent'
        b[pos+n:stop]=last+b[pos+n:stop-n];b[pos:pos+n]=rec
        p.write_bytes(b);break
    pos+=n
else:raise RuntimeError('large test record missing')
PY
case_test large FILE_TOO_LARGE
rm -rf "$out/tree"
cp -a "$BUILD/fixture" "$out/tree"
printf 'not an EFI image\n' > "$out/tree/EFI/BOOT/BOOTX64.EFI"
xorriso -as mkisofs -R -o "$out/bad-efi.iso" "$out/tree" > "$out/bad-efi-build.log" 2>&1
case_test bad-efi CHAINLOAD_FAILED
# An EFI application that returns successfully rather than transferring control.
cat > "$out/return.c" <<'C'
#include <efi.h>
#include <efilib.h>
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *table) {
  (void)image;(void)table;return EFI_SUCCESS;
}
C
efi_inc=$TOOLS/usr/include/efi
efi_lib=$TOOLS/usr/lib
if [[ ! -d $efi_inc ]]; then efi_inc=/usr/include/efi; efi_lib=/usr/lib; fi
gcc -I"$efi_inc" -I"$efi_inc/x86_64" -fpic -fshort-wchar -mno-red-zone -fno-stack-protector -DEFI_FUNCTION_WRAPPER -c "$out/return.c" -o "$out/return.o"
ld -nostdlib -znocombreloc -T "$efi_lib/elf_x86_64_efi.lds" -shared -Bsymbolic "$efi_lib/crt0-efi-x86_64.o" "$out/return.o" -L"$efi_lib" -lefi -lgnuefi -o "$out/return.so"
objcopy -j .rodata -j .dynstr -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc --target=efi-app-x86_64 "$out/return.so" "$out/tree/EFI/BOOT/BOOTX64.EFI"
xorriso -as mkisofs -R -o "$out/return.iso" "$out/tree" > "$out/return-build.log" 2>&1
case_test return BOOTLOADER_RETURNED
# A long Volume ID must not prevent successful EFI execution.
xorriso -as mkisofs -R -V LONG_VOLUME_ID_OVER_ELEVEN -o "$out/long-label.iso" "$BUILD/fixture" > "$out/long-label-build.log" 2>&1
case_test long-label 'EFI payload read from scratch successfully'
rm -rf "$out/tree"
cp -a "$BUILD/fixture" "$out/tree"
mv "$out/tree/EFI" "$out/tree/efi"
mv "$out/tree/efi/BOOT" "$out/tree/efi/boot"
mv "$out/tree/efi/boot/BOOTX64.EFI" "$out/tree/efi/boot/bootx64.efi"
xorriso -as mkisofs -R -o "$out/lowercase-efi.iso" "$out/tree" > "$out/lowercase-efi-build.log" 2>&1
case_test lowercase-efi 'EFI payload read from scratch successfully'
python3 - "$out" <<'PY'
import pathlib,sys
out=pathlib.Path(sys.argv[1])
for name in ['missing','large','link-file','link-directory','link-dangling','link-cycle','link-continuation']:
    log=(out/f'{name}.log').read_text()
    assert '[bootiso] extracting /' not in log,name
    assert 'cache committed' not in log,name
assert '4294967296' in (out/'large.log').read_text()
print('PASS: preflight rejects before extraction; missing EFI, oversized file and all link cases classified')
PY
