"""Unified components retain identical bytes without large shell function ASTs."""
from pathlib import Path
import hashlib
import io
import re
import sys
import tarfile
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'installer'))
import camera_component as camera
import network_component as network

def decode(tail):
    result = bytearray()
    for line in tail.splitlines():
        if not line or line.startswith('#'):
            continue
        match = re.fullmatch(r"printf '((?:\\[0-7]{3})+)' \|\| exit 1", line)
        assert match, line[:80]
        encoded = match[1]
        result.extend(int(encoded[i+1:i+4], 8) for i in range(0, len(encoded), 4))
    return bytes(result)

def run():
    with tempfile.TemporaryDirectory(prefix='cc2-component-stream-') as temporary:
        root = Path(temporary); source=root/'source'; runtime=root/'runtime'
        source.mkdir(); runtime.mkdir()
        for name in ('common.sh','manage.sh','boot-camera.sh'):
            (source/name).write_bytes(b'#!/bin/sh\nexit 0\n')
        saved=dict(camera.PINS)
        try:
            for name in camera.PINS:
                content=bytes(range(256))*64+name.encode()
                (runtime/name).write_bytes(content)
                camera.PINS[name]=hashlib.sha256(content).hexdigest()
            legacy=camera.functions(source,runtime)
            code,tail=camera.functions(source,runtime,streamed=True)
            assert decode(tail)==camera.component_archive(source,runtime)
            assert "printf '\\" in legacy and "printf '\\" not in code
            assert 'sha256sum' in code and 'sha256sum -c SHA256SUMS' in code
            assert '# CC2_CAMERA_DATA_BEGIN' in code and len(code)<5000
        finally:
            camera.PINS.clear();camera.PINS.update(saved)
        buffer=io.BytesIO();helper=b'fixture helper';content=bytes(range(256))*64
        with tarfile.open(fileobj=buffer,mode='w:gz') as archive:
            for name,data in [('control',helper),('fixture',content)]:
                info=tarfile.TarInfo(name);info.size=len(data);archive.addfile(info,io.BytesIO(data))
        archive_path=root/'network.tar.gz';archive_path.write_bytes(buffer.getvalue())
        saved_runtime,saved_helper=network.RUNTIME_SHA,network.HELPER_SHA
        try:
            network.RUNTIME_SHA=hashlib.sha256(buffer.getvalue()).hexdigest()
            network.HELPER_SHA=hashlib.sha256(helper).hexdigest()
            legacy=network.functions(archive_path)
            code,tail=network.functions(archive_path,streamed=True)
            assert decode(tail)==buffer.getvalue()
            assert "printf '\\" in legacy and "printf '\\" not in code
            assert network.RUNTIME_SHA in code and 'sha256sum -c SHA256SUMS' in code
            assert '# CC2_NETWORK_DATA_BEGIN' in code and len(code)<5000
            bad=bytearray(buffer.getvalue());bad[-1]^=1;archive_path.write_bytes(bad)
            try:network.functions(archive_path,streamed=True)
            except ValueError:pass
            else:raise AssertionError('Modified runtime accepted')
        finally:
            network.RUNTIME_SHA,network.HELPER_SHA=saved_runtime,saved_helper
    print('PASS: streamed camera/network bytes identical, literal payload outside functions, checks retained, tampered runtime rejected and legacy renderer preserved')

if __name__=='__main__':run()
