#!/usr/bin/env python3
"""Change ISO bytes without changing path, size or mtime in the ext4 DATA image."""
import pathlib, subprocess, sys, re, os
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
from image import PARTS
out=pathlib.Path(sys.argv[1])
replacement=pathlib.Path(sys.argv[2]).resolve()
part=out/'p2.img'
def debug(command):
    return subprocess.check_output(['debugfs','-w','-R',command,str(part)],stderr=subprocess.STDOUT)
stat=debug('stat /iso/test.iso').decode()
mtime=int(re.search(r'mtime:\s+0x([0-9a-f]+)',stat).group(1),16)
old=subprocess.check_output(['debugfs','-R','cat /iso/test.iso',str(part)],stderr=subprocess.DEVNULL)
new=replacement.read_bytes()
assert len(old)==len(new) and old!=new,'fixture must change bytes at constant size'
debug('rm /iso/test.iso')
debug(f'write {replacement} /iso/test.iso')
debug(f'set_inode_field /iso/test.iso mtime 0x{mtime:08x}')
actual=subprocess.check_output(['debugfs','-R','cat /iso/test.iso',str(part)],stderr=subprocess.DEVNULL)
assert actual==new,'debugfs write did not take effect'
stat2=debug('stat /iso/test.iso').decode()
assert int(re.search(r'mtime:\s+0x([0-9a-f]+)',stat2).group(1),16)==mtime,'mtime not preserved'
# Reuse the image constructor's sparse extent copy, but only replace DATA.
with part.open('rb') as src, (out/'disk.img').open('r+b') as dest:
    end=os.fstat(src.fileno()).st_size;pos=0
    while pos<end:
        try:begin=os.lseek(src.fileno(),pos,os.SEEK_DATA)
        except OSError as e:
            import errno
            if e.errno==errno.ENXIO:break
            raise
        stop=os.lseek(src.fileno(),begin,os.SEEK_HOLE)
        src.seek(begin);dest.seek(PARTS[1][0]*512+begin)
        while src.tell()<stop:dest.write(src.read(min(1024**2,stop-src.tell())))
        pos=stop
print('PASS: replaced ISO with equal-sized different bytes and identical path/mtime')
