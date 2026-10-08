#!/usr/bin/env python3
"""Rootless GPT image construction, without mounts or loop devices."""
import argparse, pathlib, subprocess, shutil, os, re
ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / '.build'
PARTS = [(2048, 131072, 'EF00', 'EFI'), (133120, 2097152, '8300', 'DATA'),
         (2230272, 3145728, '0700', 'SCRATCH')]
def run(*args):
    subprocess.run(list(map(str, args)), check=True)
def main():
    p = argparse.ArgumentParser()
    p.add_argument('--config', type=pathlib.Path, required=True)
    p.add_argument('--iso', type=pathlib.Path, action='append', default=[])
    p.add_argument('--name', default='test')
    p.add_argument('--reuse', action='store_true')
    a = p.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*', a.name):
        p.error('--name must be a simple directory name')
    if len({iso.name for iso in a.iso}) != len(a.iso):
        p.error('ISO basenames must be unique')
    if a.reuse and a.iso:
        p.error('--reuse updates only GRUB; omit --iso')
    out = BUILD / a.name
    out.mkdir(parents=True, exist_ok=True)
    efi = out / 'BOOTX64.EFI'
    run(BUILD/'grub-build/grub-mkstandalone', '-O', 'x86_64-efi',
        '-d', BUILD/'grub-build/grub-core', '--modules',
        'part_gpt fat ext2 iso9660 udf loopback serial terminal normal chain search search_label halt',
        '-o', efi, f'boot/grub/grub.cfg={a.config.resolve()}')
    if a.reuse:
        disk = out/'disk.img'
        if not disk.exists(): raise SystemExit('--reuse requires an existing disk')
        run('mcopy', '-o', '-i', f'{disk}@@{PARTS[0][0]*512}', efi, '::/EFI/BOOT/BOOTX64.EFI')
        print(disk)
        return
    for i, (_, sectors, _, label) in enumerate(PARTS, 1):
        part = out / f'p{i}.img'
        with part.open('wb') as f: f.truncate(sectors * 512)
        if i == 2:
            tree = out/'data'
            if tree.exists(): shutil.rmtree(tree)
            (tree/'iso').mkdir(parents=True)
            for iso in a.iso: shutil.copyfile(iso, tree/'iso'/iso.name)
            run('mkfs.ext4', '-q', '-F', '-O', '^extent,^64bit', '-L', label, '-d', tree, part)
        else:
            run('mkfs.fat', '-F', '32', '-n', label, part)
            if i == 1:
                run('mmd', '-i', part, '::/EFI', '::/EFI/BOOT')
                run('mcopy', '-i', part, efi, '::/EFI/BOOT/BOOTX64.EFI')
            else:
                marker = out/'scratch-marker'
                marker.write_text('multigrub scratch v1\n')
                run('mcopy', '-i', part, marker, '::/.multigrub-scratch')
    disk = out/'disk.img'
    with disk.open('wb') as f: f.truncate(3 * 1024**3)
    run('sgdisk', '--clear', *[s for i, (start, size, kind, label) in enumerate(PARTS, 1)
        for s in (f'--new={i}:{start}:{start+size-1}', f'--typecode={i}:{kind}', f'--change-name={i}:{label}')], disk)
    with disk.open('r+b') as dest:
        for i, (start, _, _, _) in enumerate(PARTS, 1):
            with (out/f'p{i}.img').open('rb') as src:
                base = start * 512
                end = os.fstat(src.fileno()).st_size
                pos = 0
                # Preserve sparse images: do not read gigabytes of holes.
                import errno
                while pos < end:
                    try:
                        begin = os.lseek(src.fileno(), pos, os.SEEK_DATA)
                        stop = os.lseek(src.fileno(), begin, os.SEEK_HOLE)
                    except OSError as e:
                        if e.errno == errno.ENXIO: break
                        if e.errno != errno.EINVAL: raise
                        begin, stop = pos, end
                    src.seek(begin)
                    dest.seek(base + begin)
                    while src.tell() < stop:
                        chunk = src.read(min(1024**2, stop-src.tell()))
                        if not chunk: raise RuntimeError('short partition read')
                        dest.write(chunk)
                    pos = stop
    print(disk)
if __name__ == '__main__': main()
