#!/usr/bin/env python3
import argparse, pathlib, subprocess, time, shutil
ROOT = pathlib.Path(__file__).resolve().parents[1]
def main():
    p = argparse.ArgumentParser()
    p.add_argument('disk', type=pathlib.Path)
    p.add_argument('--expect', required=True)
    p.add_argument('--timeout', type=int, default=120)
    p.add_argument('--usb', action='store_true')
    a = p.parse_args()
    share = ROOT/'.build/tools/usr/share'
    if not (share/'OVMF').exists(): share = pathlib.Path('/usr/share')
    nvram = a.disk.parent/'OVMF_VARS.fd'
    shutil.copyfile(share/'OVMF/OVMF_VARS_4M.fd', nvram)
    log = a.disk.parent/('serial-usb.log' if a.usb else 'serial.log')
    cmd = ['qemu-system-x86_64', '-machine', 'q35', '-accel', 'tcg', '-m', '512',
           '-L', str(share/'qemu'), '-display', 'none', '-vga', 'none', '-serial', f'file:{log}',
           '-monitor', 'none', '-no-reboot', '-net', 'none',
           '-drive', f'if=pflash,format=raw,readonly=on,file={share}/OVMF/OVMF_CODE_4M.fd',
           '-drive', f'if=pflash,format=raw,file={nvram}']
    drive = f'format=raw,file={a.disk.resolve()}'
    if a.usb:
        cmd += ['-device', 'qemu-xhci', '-drive', f'if=none,id=stick,{drive}',
                '-device', 'usb-storage,drive=stick,removable=on']
    else: cmd += ['-drive', f'if=ide,{drive}']
    log.write_text('')
    proc = subprocess.Popen(cmd)
    found = False
    try:
        deadline = time.monotonic() + a.timeout
        while proc.poll() is None and time.monotonic() < deadline:
            if a.expect in log.read_text(errors='replace'):
                found = True; break
            time.sleep(.25)
    finally:
        if proc.poll() is None: proc.terminate()
        proc.wait(timeout=15)
    contents = log.read_text(errors='replace')
    found = found or a.expect in contents
    print(contents)
    if not found: raise SystemExit(f'FAIL: did not observe {a.expect!r}; log: {log}')
    print(f'PASS: {a.expect}')
if __name__ == '__main__': main()
