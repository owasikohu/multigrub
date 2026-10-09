#!/usr/bin/env bash
# Optional network-dependent integration test of an unmodified stock Linux ISO.
set -euo pipefail
source "$(dirname "$0")/env.sh"
"$ROOT/scripts/build.sh"
"$ROOT/scripts/linux-iso.sh"
command -v tesseract >/dev/null
original=$BUILD/linux-validation-original
if [[ -d $original ]]; then chmod -R u+w "$original"; fi
rm -rf "$original"
mkdir -p "$original"
xorriso -osirrox on -indev "$BUILD/linux/alpine-standard-3.22.3-x86_64.iso" \
  -extract / "$original" > "$BUILD/linux-source-extract.log" 2>&1
python3 "$ROOT/scripts/image.py" --config "$ROOT/tests/linux.cfg" \
  --iso "$BUILD/linux/alpine-standard-3.22.3-x86_64.iso" --name linux-validation
disk=$BUILD/linux-validation/disk.img
python3 "$ROOT/scripts/linux-test.py" "$disk" | tee "$BUILD/linux-sata-test.log"
python3 "$ROOT/scripts/verify.py" "$disk" --stage tree --tree "$original"
python3 "$ROOT/scripts/linux-test.py" "$disk" --usb | tee "$BUILD/linux-usb-test.log"
python3 - "$BUILD/linux-validation" <<'PY'
import pathlib,re,sys
out=pathlib.Path(sys.argv[1])
sata=(out/'linux-sata/serial.log').read_text(errors='replace')
usb=(out/'linux-usb/serial.log').read_text(errors='replace')
assert 'cache miss' in sata and 'extracted 128 entries' in sata
assert 'cache hit; reusing scratch' in usb and '[bootiso] extracting /' not in usb
for serial,mountpoint in [(sata,'sda3'),(usb,'usb')]:
    assert 'MULTIGRUB_LINUX_PASS' in serial
    assert 'BOOT_IMAGE=/boot/vmlinuz-lts modules=loop,squashfs,sd-mod,usb-storage quiet' in serial
    assert f'/dev/sda3 on /media/{mountpoint} type vfat (ro,' in serial
print('PASS: stock Alpine Linux boots from extracted FAT media over SATA and USB without boot-file/cmdline changes')
PY
