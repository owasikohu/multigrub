#!/usr/bin/env python3
"""Boot a stock Alpine ISO's EFI path, observe its VGA console, login via QMP.

No guest boot arguments or files are changed. Kernel/user-space evidence is
written by the logged-in guest to the physical emulated serial port.
"""
import argparse, json, pathlib, shutil, socket, subprocess, time, re, os
ROOT = pathlib.Path(__file__).resolve().parents[1]
class Monitor:
    def __init__(self,path):
        self.sock=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
        self.sock.connect(str(path));self.sock.settimeout(5)
        self.stream=self.sock.makefile('rwb',buffering=0)
        json.loads(self.stream.readline())
        self.next_id=0
        self.call('qmp_capabilities')
    def call(self,name,args=None):
        self.next_id+=1
        request={'execute':name,'id':self.next_id}
        if args is not None:request['arguments']=args
        self.stream.write((json.dumps(request)+'\n').encode())
        while True:
            message=json.loads(self.stream.readline())
            if message.get('id')!=self.next_id:continue
            if 'error' in message:raise RuntimeError(message['error'])
            return message.get('return')
    def text(self,text):
        keys={' ':'spc','/':'slash','.':'dot','-':'minus','_':'shift-minus',
              '>':'shift-dot',';':'semicolon',':':'shift-semicolon',
              '(':'shift-9',')':'shift-0','\n':'ret','=':'equal',
              '|':'shift-backslash','"':'shift-apostrophe',"'":'apostrophe',
              '[':'bracket_left',']':'bracket_right','$':'shift-4'}
        for char in text:
            key=keys.get(char)
            if key is None:
                if char.isascii() and char.isalnum():
                    key=('shift-'+char.lower()) if char.isupper() else char
                else:raise ValueError(f'unsupported guest key {char!r}')
            self.call('human-monitor-command',{'command-line':f'sendkey {key} 30'})
            time.sleep(.07)
    def close(self):self.stream.close();self.sock.close()
def main():
    p=argparse.ArgumentParser()
    p.add_argument('disk',type=pathlib.Path)
    p.add_argument('--usb',action='store_true')
    p.add_argument('--timeout',type=int,default=600)
    p.add_argument('--kernel-release',default='6.12.67-0-lts',help='Expected stock kernel release for this test image')
    p.add_argument('--expected-cmdline',default='BOOT_IMAGE=/boot/vmlinuz-lts modules=loop,squashfs,sd-mod,usb-storage quiet',help='Validate original cmdline; never writes it to the guest')
    a=p.parse_args()
    share=ROOT/'.build/tools/usr/share'
    if not (share/'OVMF').exists():share=pathlib.Path('/usr/share')
    out=a.disk.resolve().parent/('linux-usb' if a.usb else 'linux-sata')
    out.mkdir(exist_ok=True)
    nvram=out/'OVMF_VARS.fd';shutil.copyfile(share/'OVMF/OVMF_VARS_4M.fd',nvram)
    log=out/'serial.log';log.write_text('')
    sock=out/'qmp.sock'
    if sock.exists():sock.unlink()
    cmd=['qemu-system-x86_64','-machine','q35','-accel','tcg','-m','1024',
         '-L',str(share/'qemu'),'-display','none','-vga','none',
         '-device',f'VGA,romfile={share}/seabios/vgabios-stdvga.bin',
         '-serial',f'file:{log}','-monitor','none','-qmp',f'unix:{sock},server=on,wait=off',
         '-no-reboot','-net','none',
         '-drive',f'if=pflash,format=raw,readonly=on,file={share}/OVMF/OVMF_CODE_4M.fd',
         '-drive',f'if=pflash,format=raw,file={nvram}']
    drive=f'format=raw,file={a.disk.resolve()}'
    if a.usb:cmd+=['-device','qemu-xhci','-drive',f'if=none,id=stick,{drive}',
                  '-device','usb-storage,drive=stick,removable=on']
    else:cmd+=['-drive',f'if=ide,{drive}']
    proc=subprocess.Popen(cmd);monitor=None;success=False
    logged_in=False;command_sent=False;next_capture=0
    try:
        deadline=time.monotonic()+a.timeout
        while not sock.exists() and proc.poll() is None and time.monotonic()<deadline:time.sleep(.1)
        monitor=Monitor(sock)
        while proc.poll() is None and time.monotonic()<deadline:
            serial=log.read_text(errors='replace')
            lines=serial.replace('\r','').splitlines()
            expected_cmdline=a.expected_cmdline
            if ('MULTIGRUB_LINUX_PASS' in lines
                and any(re.search(rf'(?:^|localhost login: )Linux localhost {re.escape(a.kernel_release)} .* x86_64 Linux$',line) for line in lines)
                and any(line.strip()==expected_cmdline for line in lines)
                and any(line.startswith(f'/dev/sda3 on /media/{"usb" if a.usb else "sda3"} type vfat (ro,') for line in lines)):
                success=True;break
            if time.monotonic()>=next_capture:
                monitor.call('screendump',{'filename':str(out/'console.ppm')})
                ocr=subprocess.run(['tesseract',str(out/'console.ppm'),'stdout','--psm','6'],
                                   check=True,capture_output=True,text=True,
                                   env={**os.environ,'OMP_THREAD_LIMIT':'1'}).stdout
                (out/'console.txt').write_text(ocr)
                print(ocr[-1800:],flush=True)
                if not logged_in and re.search(r'\blogin\s*:',ocr,re.I):
                    monitor.text('root\n');logged_in=True
                elif logged_in and not command_sent and re.search(r'localhost.*[~:#]',ocr):
                    monitor.text('echo >/dev/ttyS0; uname -a >/dev/ttyS0; cat /proc/cmdline >/dev/ttyS0; mount >/dev/ttyS0; echo MULTIGRUB_LINUX_PASS >/dev/ttyS0\n')
                    command_sent=True
                next_capture=time.monotonic()+5
            time.sleep(.25)
    finally:
        if monitor:monitor.close()
        if proc.poll() is None:proc.terminate()
        proc.wait(timeout=15)
    print(log.read_text(errors='replace'))
    if not success:raise SystemExit(f'FAIL: stock Linux did not reach verified user space; artifacts: {out}')
    print(f'PASS: stock Linux kernel and logged-in user space ({"USB" if a.usb else "SATA"})')
if __name__=='__main__':main()
