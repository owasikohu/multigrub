#!/usr/bin/env python3
"""Observe a generic scratch boot or an explicitly separate optical control.

Does not modify ISO boot files or kernel arguments. Screenshots/OCR and serial
are evidence for manual assessment; observing a screen alone is not a PASS.
"""
import argparse, importlib.util, json, os, pathlib, re, shutil, subprocess, time
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('console_monitor',ROOT/'scripts/linux-test.py')
helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
p=argparse.ArgumentParser()
p.add_argument('media',type=pathlib.Path)
p.add_argument('--optical-control',action='store_true',help='Original ISO control only, not the extraction workflow')
p.add_argument('--out',type=pathlib.Path,required=True)
p.add_argument('--timeout',type=int,default=300)
p.add_argument('--memory',type=int,default=1024)
p.add_argument('--cpu',default='qemu64',help='Generic emulated CPU capabilities; no guest boot-file changes')
p.add_argument('--network',choices=['none','user'],default='none',help='Optional normal virtio NIC with QEMU user networking')
p.add_argument('--expect-screen',help='Regex for an independently identified final user-space screen')
p.add_argument('--send-key',help='One normal boot-menu key (QEMU key spelling)')
p.add_argument('--key-after',type=int,default=15)
a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
for pattern in ['screen-*.ppm','screen-*.txt','observation.json','qemu-command.json']:
 for stale in out.glob(pattern):stale.unlink()
share=ROOT/'.build/tools/usr/share'
if not (share/'OVMF').exists():share=pathlib.Path('/usr/share')
vars=out/'OVMF_VARS.fd';shutil.copyfile(share/'OVMF/OVMF_VARS_4M.fd',vars)
log=out/'serial.log';log.write_text('');sock=out/'qmp.sock'
if sock.exists():sock.unlink()
cmd=['qemu-system-x86_64','-machine','q35','-accel','tcg','-cpu',a.cpu,'-m',str(a.memory),
 '-L',str(share/'qemu'),'-display','none','-vga','none',
 '-device',f'VGA,romfile={share}/seabios/vgabios-stdvga.bin',
 '-serial',f'file:{log}','-monitor','none','-qmp',f'unix:{sock},server=on,wait=off',
 '-no-reboot',
 '-drive',f'if=pflash,format=raw,readonly=on,file={share}/OVMF/OVMF_CODE_4M.fd',
 '-drive',f'if=pflash,format=raw,file={vars}']
if a.network=='user':cmd+=['-netdev','user,id=network0','-device','virtio-net-pci,netdev=network0']
else:cmd+=['-net','none']
if a.optical_control:
 cmd+=['-drive',f'if=ide,media=cdrom,readonly=on,format=raw,file={a.media.resolve()}', '-boot','d']
else:cmd+=['-drive',f'if=ide,format=raw,file={a.media.resolve()}']
proc=subprocess.Popen(cmd);monitor=None;reason=None;code=None;key_sent=False;captures=0
started=time.monotonic();next_capture=started
try:
 deadline=started+a.timeout
 while not sock.exists() and proc.poll() is None and time.monotonic()<deadline:time.sleep(.1)
 monitor=helper.Monitor(sock)
 while proc.poll() is None and time.monotonic()<deadline:
  serial=log.read_text(errors='replace')
  if a.send_key and not key_sent and time.monotonic()-started>=a.key_after:
   monitor.call('human-monitor-command',{'command-line':f'sendkey {a.send_key} 100'});key_sent=True
  if time.monotonic()>=next_capture:
   captures+=1;screen=out/f'screen-{captures:03}.ppm'
   monitor.call('screendump',{'filename':str(screen)})
   text=subprocess.run(['tesseract',str(screen),'stdout','--psm','6'],check=True,capture_output=True,text=True,
      env={**os.environ,'OMP_THREAD_LIMIT':'1'}).stdout
   (out/f'screen-{captures:03}.txt').write_text(text)
   print(text[-1800:],flush=True)
   next_capture=time.monotonic()+10
   if not a.optical_control:
    m=re.search(r'\[bootiso\] (NO_EFI_LOADER|FILE_TOO_LARGE|SYMLINK_UNSUPPORTED|EXTRACTION_FAILED|CHAINLOAD_FAILED|BOOTLOADER_RETURNED):',serial)
    if m:code=m[1];reason='diagnostic';break
   if 'X64 Exception Type' in serial:reason='firmware-exception';break
   if 'Kernel panic' in serial:reason='kernel-panic';break
   if a.expect_screen and re.search(a.expect_screen,text,re.I):reason='expected-user-space-screen';break
  time.sleep(.25)
finally:
 if monitor:monitor.close()
 if proc.poll() is None:proc.terminate()
 proc.wait(timeout=15)
serial=log.read_text(errors='replace');(out/'qemu-command.json').write_text(json.dumps(cmd,indent=2))
result={'mode':'original-optical-control' if a.optical_control else 'generic-extraction',
 'reason':reason or 'timeout-or-exit','code':code,'captures':captures,
 'elapsed_seconds':round(time.monotonic()-started,1)}
(out/'observation.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result),flush=True)
