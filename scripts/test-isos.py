#!/usr/bin/env python3
"""Replay checksum-pinned real ISOs using the unchanged extraction workflow."""
import argparse, hashlib, json, pathlib, re, shutil, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
BUILD=ROOT/'.build'
def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def run(command,log):
    with log.open('w') as stream:
        return subprocess.run(list(map(str,command)),cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT).returncode
def main():
    catalog=json.loads((ROOT/'tests/iso-matrix.json').read_text())['isos']
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--only',choices=[row['id'] for row in catalog],action='append')
    p.add_argument('--download-only',action='store_true')
    a=p.parse_args();results=[]
    selected=[row for row in catalog if not a.only or row['id'] in a.only]
    downloads=BUILD/'iso-matrix';downloads.mkdir(parents=True,exist_ok=True)
    for row in selected:
        ident=row['id'];filename=row['filename']
        if not re.fullmatch(r'[A-Za-z0-9_.-]+',ident) or not re.fullmatch(r'[A-Za-z0-9_.-]+',filename):
            raise ValueError('unsafe catalog filename')
        iso=downloads/filename
        if not iso.exists() or digest(iso)!=row['sha256']:
            temporary=iso.with_suffix('.iso.partial')
            subprocess.run(['curl','--fail','--location','--max-time','600',row['url'],'-o',str(temporary)],check=True)
            if digest(temporary)!=row['sha256']:raise RuntimeError(f'{ident}: upstream bytes differ from the pinned ISO')
            temporary.replace(iso)
        print(f'Checksum verified: {ident}',flush=True)
        if a.download_only:continue
        out=BUILD/f'matrix-{ident}';out.mkdir(exist_ok=True)
        config=out/'grub.cfg'
        config.write_text('serial --unit=0 --speed=115200\nterminal_output console serial\n'
          'insmod hello\ninsmod chain\ninsmod boot\ninsmod gcry_sha256\ninsmod sleep\n'
          f"search --label DATA --set=root\nbootiso_boot '/iso/{filename}'\nsleep 90\nhalt\n")
        subprocess.run([sys.executable,str(ROOT/'scripts/image.py'),'--config',str(config),
          '--iso',str(iso),'--name',f'matrix-{ident}','--data-mib',str(row['data_mib']),
          '--scratch-mib',str(row['scratch_mib'])],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
        disk=out/'disk.img';log=out/'test.log';kind=row['test']
        if kind=='alpine':
            command=[sys.executable,ROOT/'scripts/linux-test.py',disk,'--timeout',str(row['timeout']),
              '--kernel-release',row['kernel_release'],'--expected-cmdline',row['expected_cmdline']]
            code=run(command,log)
            stage='USER_SPACE' if code==0 else 'BOOT'
            if code==0:
                original=out/'original'
                if original.exists():
                    subprocess.run(['chmod','-R','u+w',str(original)],check=True)
                    shutil.rmtree(original)
                original.mkdir()
                subprocess.run(['xorriso','-indev',str(iso),'-osirrox','on','-extract','/',str(original)],
                  stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
                offset=json.loads((out/'partitions.json').read_text())[2]['offset']
                code=run([sys.executable,ROOT/'scripts/verify.py',disk,'--tree',original,
                  '--stage','tree','--scratch-offset',str(offset)],out/'byte-compare.log')
                if code:stage='BYTE_COMPARE'
            result={'id':ident,'status':'PASS' if code==0 else 'FAIL','stage':stage,
                    'exit_code':code,'log':str(log.relative_to(ROOT))}
        else:
            observed=out/'replay-observed'
            command=[sys.executable,ROOT/'scripts/iso-observe.py',disk,'--out',observed,
              '--timeout',str(row['timeout']),'--cpu',row.get('cpu','qemu64'),
              '--memory',str(row.get('memory',1024)),'--network',row.get('network','none')]
            if row.get('expect_screen'):command+=['--expect-screen',row['expect_screen']]
            code=run(command,log)
            artifact=observed/'observation.json'
            observation=json.loads(artifact.read_text()) if code==0 and artifact.exists() else {}
            diagnostic=observation.get('code')
            if diagnostic in ['NO_EFI_LOADER','FILE_TOO_LARGE','SYMLINK_UNSUPPORTED']:status='UNSUPPORTED';stage='PREFLIGHT'
            elif observation.get('reason')=='expected-user-space-screen':status='PASS';stage='USER_SPACE_SCREEN'
            else:status='FAIL';stage=observation.get('reason','HARNESS')
            result={'id':ident,'status':status,'stage':stage,'code':diagnostic,'exit_code':code,
                    'log':str(log.relative_to(ROOT))}
            if row.get('expected_code') and diagnostic!=row['expected_code']:result['unexpected_result']=True
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        results.append(result);print(json.dumps(result),flush=True)
        (downloads/'replay-results.json').write_text(json.dumps(results,indent=2)+'\n')
    if any(row['status']=='FAIL' or row.get('unexpected_result') for row in results):raise SystemExit(1)
if __name__=='__main__':main()
