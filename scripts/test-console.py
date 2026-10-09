#!/usr/bin/env python3
"""Assert a GRUB diagnostic is present on both serial and the QEMU VGA console."""
import argparse, importlib.util, pathlib, re, shutil, subprocess, time
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('linux_console',ROOT/'scripts/linux-test.py')
helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
p=argparse.ArgumentParser();p.add_argument('disk',type=pathlib.Path);p.add_argument('--expect',required=True)
a=p.parse_args();out=a.disk.resolve().parent/'console-check';out.mkdir(exist_ok=True)
share=ROOT/'.build/tools/usr/share'
if not (share/'OVMF').exists():share=pathlib.Path('/usr/share')
vars=out/'OVMF_VARS.fd';shutil.copyfile(share/'OVMF/OVMF_VARS_4M.fd',vars)
log=out/'serial.log';log.write_text('');sock=out/'qmp.sock'
if sock.exists():sock.unlink()
proc=subprocess.Popen(['qemu-system-x86_64','-machine','q35','-accel','tcg','-m','512',
 '-L',str(share/'qemu'),'-display','none','-vga','none',
 '-device',f'VGA,romfile={share}/seabios/vgabios-stdvga.bin',
 '-serial',f'file:{log}','-monitor','none','-qmp',f'unix:{sock},server=on,wait=off',
 '-net','none','-no-reboot',
 '-drive',f'if=pflash,format=raw,readonly=on,file={share}/OVMF/OVMF_CODE_4M.fd',
 '-drive',f'if=pflash,format=raw,file={vars}',
 '-drive',f'if=ide,format=raw,file={a.disk.resolve()}'])
monitor=None;passed=False
try:
 deadline=time.monotonic()+120
 while not sock.exists() and time.monotonic()<deadline and proc.poll() is None:time.sleep(.1)
 monitor=helper.Monitor(sock)
 while time.monotonic()<deadline and proc.poll() is None:
  if a.expect in log.read_text(errors='replace'):
   monitor.call('screendump',{'filename':str(out/'screen.ppm')})
   text=subprocess.run(['tesseract',str(out/'screen.ppm'),'stdout','--psm','6'],check=True,capture_output=True,text=True).stdout
   (out/'screen.txt').write_text(text)
   normalize=lambda s:re.sub('[^A-Z0-9]','',s.upper())
   if normalize(a.expect) in normalize(text):passed=True;break
  time.sleep(1)
finally:
 if monitor:monitor.close()
 if proc.poll() is None:proc.terminate()
 proc.wait(timeout=15)
if not passed:raise SystemExit(f'FAIL: serial/VGA diagnostic check; artifacts {out}')
print(f'PASS: {a.expect} on both serial and VGA console')
