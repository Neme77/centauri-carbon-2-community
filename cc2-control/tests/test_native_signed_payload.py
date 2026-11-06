"""Verify/extract the actual common installer payload with disposable keys.

This checks the existing signed-update format/allowlist on host and ARM without
accessing a release key or executing any extracted application/native program.
"""
from pathlib import Path
import gzip,hashlib,io,json,struct,subprocess,sys,tarfile,tempfile

APP=Path(__file__).resolve().parents[1]


def run(payload_path):
    payload=gzip.decompress(payload_path.read_bytes())
    assert 1024<=len(payload)<=32*1024*1024
    with tarfile.open(fileobj=io.BytesIO(payload),mode='r:') as archive:
        expected={m.name:archive.extractfile(m).read() for m in archive if m.isfile()}
    count=0
    def command(args):
        result=subprocess.run(list(map(str,args)),capture_output=True,timeout=120)
        if result.returncode:
            # Retain compiler/verifier diagnostics instead of hiding stderr in
            # CalledProcessError. No release/private key is used by this test.
            sys.stderr.write(result.stderr.decode('utf-8',errors='replace'))
            result.check_returncode()
        return result
    with tempfile.TemporaryDirectory(prefix='cc2-native-signed-payload-') as temporary:
        base=Path(temporary);key=base/'disposable.pem'
        command(['openssl','genpkey','-algorithm','ED25519','-out',key])
        public=command(['openssl','pkey','-in',key,'-pubout','-outform','DER']).stdout[-32:]
        (base/'trusted-key.h').write_text('static const unsigned char cc2_update_trusted_key[32]={'+','.join(map(str,public))+'};\n')
        (base/'trusted-base.h').write_bytes((APP/'updater/trusted-base.h').read_bytes())
        (base/'verify.c').write_bytes((APP/'updater/verify.c').read_bytes())
        compatibility=json.loads((APP/'installer/compatibility.json').read_text())
        manifest=(f'version=1.2.26\nfirmware=4.4\nprinter={compatibility["printer_sha256"]}\n'
            f'daemon={compatibility["daemon_sha256"]}\nsize={len(payload)}\nsha512={hashlib.sha512(payload).hexdigest()}\n').encode()
        header=b'CC2UPD1\n'+struct.pack('>IQ',len(manifest),len(payload))
        (base/'message').write_bytes(header+manifest)
        command(['openssl','pkeyutl','-sign','-rawin','-inkey',key,'-in',base/'message','-out',base/'signature'])
        signed=header+(base/'signature').read_bytes()+manifest+payload
        package=base/'test-signed-update';package.write_bytes(signed)
        for arch,compiler,flags,runner in [('host','gcc',[],[]),
            # Keep the executable ARM32, with large-file off_t for the verifier's 64-bit stream bounds.
            ('arm','arm-linux-gnueabihf-gcc',['-static','-D_TIME_BITS=32','-D_FILE_OFFSET_BITS=64'],['qemu-arm'])]:
            binary=base/('verify-'+arch)
            command([compiler,'-O2','-std=c11','-Wall','-Wextra','-Werror',*flags,
                '-DCC2_CONTROL_VERSION="1.2.25"','-DCC2_COMMUNITY_FIRMWARE_VERSION="4.4"',
                '-I'+str(APP/'third-party/monocypher'),base/'verify.c',APP/'third-party/monocypher/monocypher.c',
                APP/'third-party/monocypher/monocypher-ed25519.c','-o',binary])
            metadata=json.loads(command([*runner,binary,'--verify',package]).stdout)
            assert metadata['version']=='1.2.26';count+=1
            extracted=base/('extracted-'+arch);extracted.mkdir(mode=0o700)
            command([*runner,binary,'--extract',package,extracted])
            assert {p.relative_to(extracted).as_posix():p.read_bytes() for p in extracted.rglob('*') if p.is_file()}==expected
            count+=1
            bad=bytearray(signed);bad[-1]^=1;(base/'bad').write_bytes(bad)
            result=subprocess.run([*runner,binary,'--verify',base/'bad'],capture_output=True,timeout=120)
            assert result.returncode!=0;count+=1
    print(f'PASS: {count} actual common-payload signature/extraction/tamper cases on host/ARM, disposable keys only; extracted programs never run',flush=True)
    return count

if __name__=='__main__':run(Path(sys.argv[1]))
