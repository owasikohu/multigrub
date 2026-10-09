#!/usr/bin/env python3
"""Verify firmware-written scratch bytes from the stopped VM on the host."""
import argparse, pathlib, subprocess, tempfile, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from image import PARTS
ROOT = pathlib.Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser()
    p.add_argument('disk',type=pathlib.Path)
    p.add_argument('--tree',type=pathlib.Path,default=ROOT/'.build/fixture')
    p.add_argument('--stage',choices=['write','file','tree'],required=True)
    p.add_argument('--scratch-offset',type=int,default=PARTS[2][0]*512,help='Byte offset from generated partitions.json for custom-sized images')
    a=p.parse_args()
    scratch=f'{a.disk}@@{a.scratch_offset}'
    esp=f'{a.disk}@@{PARTS[0][0]*512}'
    if a.stage=='write':
        got=subprocess.check_output(['mtype','-i',scratch,'::/test.txt'])
        assert got==b'written by UEFI file protocol\n', got
        result=subprocess.run(['mdir','-i',esp,'::/test.txt'],capture_output=True)
        assert result.returncode!=0, 'write_test unexpectedly touched ESP'
    elif a.stage=='file':
        got=subprocess.check_output(['mtype','-i',scratch,'::/README.TXT'])
        assert got==(a.tree/'README.TXT').read_bytes(), got
    else:
        with tempfile.TemporaryDirectory() as d:
            for source in a.tree.rglob('*'):
                path=source.relative_to(a.tree).as_posix()
                if source.is_dir():
                    subprocess.run(['mdir','-i',scratch,f'::/{path}'],check=True,stdout=subprocess.DEVNULL)
                else:
                    target=pathlib.Path(d)/'file'
                    subprocess.run(['mcopy','-o','-i',scratch,f'::/{path}',str(target)],check=True)
                    assert source.read_bytes()==target.read_bytes(), f'byte mismatch: {path}'
        result=subprocess.run(['mdir','-i',scratch,'::/test.txt'],capture_output=True)
        assert result.returncode!=0,'stale scratch file survived extraction'
    print(f'PASS: host verification ({a.stage})')
if __name__=='__main__': main()
