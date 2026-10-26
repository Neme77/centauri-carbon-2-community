"""Exercise signatures, version/base checks and strict extraction with disposable keys."""
import hashlib
import io
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
def run(*args, **kwargs):
    return subprocess.run(list(map(str, args)), check=True, capture_output=True, **kwargs)

with tempfile.TemporaryDirectory(prefix='cc2-signed-update-') as temp:
    base = Path(temp)
    key = base / 'test-key.pem'
    run('openssl', 'genpkey', '-algorithm', 'ED25519', '-out', key)
    public = run('openssl', 'pkey', '-in', key, '-pubout', '-outform', 'DER').stdout[-32:]
    (base / 'trusted-key.h').write_text('static const unsigned char cc2_update_trusted_key[32]={' + ','.join(map(str,public)) + '};\n')
    (base / 'trusted-base.h').write_bytes((ROOT / 'updater/trusted-base.h').read_bytes())
    (base / 'verify.c').write_bytes((ROOT / 'updater/verify.c').read_bytes())
    binary = base / 'verify'
    flags = shlex.split(os.environ.get('CC2_TEST_CFLAGS',''))
    run('cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-DCC2_CONTROL_VERSION="1.2.21"', '-DCC2_COMMUNITY_FIRMWARE_VERSION="4.4"',
        *flags, '-I'+str(ROOT/'third-party/monocypher'), base/'verify.c', ROOT/'third-party/monocypher/monocypher.c',
        ROOT/'third-party/monocypher/monocypher-ed25519.c', '-o', binary)
    compatibility = json.loads((ROOT/'installer/compatibility.json').read_text())
    arm = bytearray(24); arm[:6]=b'\x7fELF\x01\x01'; arm[18]=40
    names = ['cc2-control','cc2-update-worker','cc2-https-download','start.sh','launch.sh','cc2-control.init',
        'install-on-printer.sh','restore-on-printer.sh','SHA256SUMS','build-info.json','update-runner.sh',
        'updater-trusted-key.json','THIRD_PARTY_NOTICES.txt','ca-bundle.pem','web/index.html','web/locales/en.json']
    def tar(extra=None, omit=None):
        data=io.BytesIO()
        with tarfile.open(fileobj=data,mode='w',format=tarfile.USTAR_FORMAT) as archive:
            for name in names:
                if name==omit: continue
                content=bytes(arm) if name in ('cc2-control','cc2-update-worker','cc2-https-download') else b'fixture\n'
                member=tarfile.TarInfo(name); member.size=len(content); member.mode=0o644
                archive.addfile(member,io.BytesIO(content))
            if extra:
                member=tarfile.TarInfo(extra[0]); member.size=len(extra[1]); member.type=extra[2]
                archive.addfile(member,io.BytesIO(extra[1]) if member.isfile() else None)
        return data.getvalue()
    def sign(message):
        (base/'message').write_bytes(message)
        run('openssl','pkeyutl','-sign','-rawin','-inkey',key,'-in',base/'message','-out',base/'signature')
        return (base/'signature').read_bytes()
    def package(payload, version='1.2.22', firmware='4.4', printer=None):
        manifest=(f'version={version}\nfirmware={firmware}\nprinter={printer or compatibility["printer_sha256"]}\n'
            f'daemon={compatibility["daemon_sha256"]}\nsize={len(payload)}\nsha512={hashlib.sha512(payload).hexdigest()}\n').encode()
        header=b'CC2UPD1\n'+struct.pack('>IQ',len(manifest),len(payload))
        return header+sign(header+manifest)+manifest+payload
    def check(data, success, extract=False):
        file=base/'package'; file.write_bytes(data)
        args=[str(binary),'--verify',str(file)]
        if extract:
            stage=base/('stage-'+str(len(list(base.glob('stage-*'))))); stage.mkdir(mode=0o700)
            args=[str(binary),'--extract',str(file),str(stage)]
        result=subprocess.run(args,capture_output=True)
        assert (result.returncode==0)==success,result.stderr.decode()
        if success: assert json.loads(result.stdout)['version']=='1.2.22'
    valid=package(tar()); check(valid,True); check(valid,True,True)
    for offset in (0,20,90,len(valid)-1):
        bad=bytearray(valid); bad[offset]^=1; check(bad,False)
    check(valid[:-1],False); check(valid+b'x',False)
    for version in ('1.2.21','1.2.20','1.2.22-test'):
        check(package(tar(),version=version),False)
    check(package(tar(),firmware='4.3'),False)
    check(package(tar(),printer='0'*64),False)
    for extra in (('../escape',b'x',tarfile.REGTYPE),('cc2-control',bytes(arm),tarfile.REGTYPE),('web/link',b'',tarfile.SYMTYPE)):
        check(package(tar(extra)),False,True)
    check(package(tar(omit='cc2-https-download')),False,True)
    message=b'CC2CAT1\n'+b'{"schema":1,"expires":2000000000,"releases":[]}'
    catalog=message[:8]+sign(message)+message[8:]
    file=base/'catalog';file.write_bytes(catalog)
    assert json.loads(run(binary,'--catalog',file).stdout)['schema']==1
    bad=bytearray(catalog);bad[-2]^=1;file.write_bytes(bad)
    assert subprocess.run([str(binary),'--catalog',str(file)],capture_output=True).returncode!=0
print('PASS: disposable-key signatures, corruption, truncation, downgrade/base rejection, strict archive and signed catalog')
