#!/usr/bin/env bash
# Meaningful end-to-end checks against an actual writable GPT image in QEMU.
set -euo pipefail
source "$(dirname "$0")/env.sh"
"$ROOT/scripts/build.sh"
"$ROOT/scripts/fixture.sh"
mkdir -p "$BUILD/conflict/EFI/BOOT"
cp "$BUILD/fixture/EFI/BOOT/BOOTX64.EFI" "$BUILD/conflict/EFI/BOOT/BOOTX64.EFI"
mkdir -p "$BUILD/conflict" "$BUILD/logs"
printf 'first\n' > "$BUILD/conflict/name.txt"
printf 'second\n' > "$BUILD/conflict/NAME.TXT"
xorriso -as mkisofs -R -V CONFLICT -o "$BUILD/conflict.iso" "$BUILD/conflict" > "$BUILD/conflict-iso.log" 2>&1
out=$BUILD/validation
disk=$out/disk.img
python3 "$ROOT/scripts/image.py" --config "$ROOT/tests/m1.cfg" \
  --iso "$BUILD/test.iso" --iso "$BUILD/conflict.iso" --name validation
boot_stage() {
  local config=$1 marker=$2 name=$3
  python3 "$ROOT/scripts/image.py" --config "$ROOT/$config" --name validation --reuse
  python3 "$ROOT/scripts/qemu.py" "$disk" --expect "$marker"
  cp "$out/serial.log" "$BUILD/logs/$name.log"
}
boot_stage tests/m1.cfg 'GRUB started successfully' m1
boot_stage tests/m2.cfg 'hello from custom grub module' m2
boot_stage tests/m3.cfg 'M3 PASS' m3
python3 "$ROOT/scripts/verify.py" "$disk" --stage write
boot_stage tests/m4.cfg 'M4 PASS' m4
python3 "$ROOT/scripts/verify.py" "$disk" --stage file
boot_stage tests/m5.cfg 'M5 PASS' m5
python3 "$ROOT/scripts/verify.py" "$disk" --stage tree
mlabel -i "$disk@@1141899264" -s :: | tee "$BUILD/logs/m8-label.log"
python3 - "$BUILD/logs/m8-label.log" <<'PY'
import pathlib,sys
assert 'MULTIGRUB' in pathlib.Path(sys.argv[1]).read_text()
PY
boot_stage tests/m6.cfg 'EFI payload read from scratch successfully' m6
boot_stage tests/m9.cfg 'EFI payload read from scratch successfully' m9
# Repeat with a real USB Mass Storage device, using retained scratch cache.
python3 "$ROOT/scripts/qemu.py" "$disk" --usb --expect 'EFI payload read from scratch successfully'
cp "$out/serial-usb.log" "$BUILD/logs/usb.log"
python3 - "$BUILD/logs" <<'PY'
import pathlib,sys
logs=pathlib.Path(sys.argv[1])
for name in ['m6','m9','usb']:
    assert 'cache hit; reusing scratch' in (logs/f'{name}.log').read_text(),name
assert 'ISO menu entry: test.iso' in (logs/'m9.log').read_text()
assert 'ISO menu entry: conflict.iso' in (logs/'m9.log').read_text()
print('PASS: cache reuse, two-entry menu, and USB chainload')
PY
boot_stage tests/reject.cfg 'REJECT PASS' reject-traversal
# Different bytes, same size/path/mtime must cause a cache miss.
rm -rf "$BUILD/changed-fixture"
cp -a "$BUILD/fixture" "$BUILD/changed-fixture"
printf 'CHANGED in ISO!\n' > "$BUILD/changed-fixture/README.TXT"
xorriso -as mkisofs -R -J -V MULTIGRUB -o "$BUILD/changed.iso" "$BUILD/changed-fixture" > "$BUILD/changed-iso.log" 2>&1
python3 "$ROOT/scripts/cache-change.py" "$out" "$BUILD/changed.iso"
boot_stage tests/cache.cfg 'CACHE TEST PASS' cache-change
python3 - "$out" "$BUILD/logs/cache-change.log" <<'PY'
import pathlib,subprocess,sys
out=pathlib.Path(sys.argv[1]);log=pathlib.Path(sys.argv[2]).read_text()
assert 'cache miss' in log and 'cache committed' in log
assert subprocess.check_output(['mtype','-i',f'{out}/disk.img@@1141899264','::/README.TXT'])==b'CHANGED in ISO!\n'
print('PASS: SHA256 rejects stale cache at identical path/size/mtime')
PY
# A FAT case collision must fail, and must not leave a success cache behind.
boot_stage tests/conflict.cfg 'CONFLICT PASS' conflict
python3 - "$disk" "$BUILD/logs/conflict.log" <<'PY'
import pathlib,subprocess,sys
assert 'FAT name collision' in pathlib.Path(sys.argv[2]).read_text()
r=subprocess.run(['mdir','-i',f'{sys.argv[1]}@@1141899264','::/.bootiso-cache'],capture_output=True)
assert r.returncode!=0, 'failed extraction retained a success cache'
print('PASS: FAT collision rejected and failed extraction not cached')
PY
printf 'All required synthetic ISO tests passed. Linux distribution test remains separate.\n'
