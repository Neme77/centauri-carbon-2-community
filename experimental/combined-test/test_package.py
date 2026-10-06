#!/usr/bin/env python3
"""Validate archive assembly with synthetic ELF fixtures, never printer binaries."""
import contextlib
import gzip
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile
import tempfile
import unittest
import zipfile
HERE=Path(__file__).resolve().parent

class PackagingTests(unittest.TestCase):
    def test_archive_integrity_and_source_provenance(self):
        spec=importlib.util.spec_from_file_location('combined_packager',HERE/'build.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(prefix='cc2-combined-package-') as temporary:
            root=Path(temporary);kit=root/'experimental/combined-test';kit.mkdir(parents=True)
            cc2=root/'cc2-control';cc2.mkdir()
            for name in ('scripts','web'):
                shutil.copytree(module.CC2/name,cc2/name)
            for path in HERE.glob('*'):
                if path.is_file():shutil.copy2(path,kit/path.name)
            shutil.copytree(HERE/'reactor',kit/'reactor')
            data=bytearray(64);data[:6]=b'\x7fELF\x01\x01';struct.pack_into('<H',data,18,40)
            def mock_run(args,**kwargs):
                args=list(map(str,args));stdout=''
                if '-dumpversion' in args:stdout='6.5.0\n'
                elif '-print-file-name=libc.so.6' in args:stdout='/fixture/libc.so.6\n'
                elif args[0]=='strings':stdout='release version 2.23\n'
                elif '--symbols' in args:stdout='\n'.join('1: 00001 4 FUNC LOCAL DEFAULT 10 '+name for name in ('cc2_callback','cc2_timers','cc2_poll','cc2_wait'))
                elif '-SW' in args:stdout='.ARM.exidx\n'
                elif '--version-info' in args:stdout='GLIBC_2.4 GLIBCXX_3.4.21\n'
                elif args[0]=='make':
                    (cc2/'dist/cc2-control').mkdir(parents=True,exist_ok=True)
                    (cc2/'build').mkdir(exist_ok=True)
                    (cc2/'dist/cc2-control/cc2-control').write_bytes(data)
                    (cc2/'build/cc2-uds-probe').write_bytes(data)
                elif '-o' in args:Path(args[args.index('-o')+1]).write_bytes(data)
                return subprocess.CompletedProcess(args,0,stdout)
            module.ROOT=root;module.CC2=cc2;module.HERE=kit;module.run=mock_run
            with contextlib.redirect_stdout(io.StringIO()):module.main()
            archive=root/'output/CC2-Control-1.1.31-UDS88-Reactor-O0-Test.zip'
            with zipfile.ZipFile(archive) as public:
                prefix=archive.stem+'/'
                checks=public.read(prefix+'SHA256SUMS').decode().splitlines()
                for line in checks:
                    expected,name=line.split('  ',1)
                    self.assertEqual(hashlib.sha256(public.read(prefix+name)).hexdigest(),expected)
                self.assertIn(b'-ResetHostKey %*',public.read(prefix+'Install-Windows.cmd'))
                sources=public.read(prefix+'combined-sources.zip')
                with tarfile.open(fileobj=io.BytesIO(public.read(prefix+'combined-payload.tar.gz')),mode='r:gz') as payload:
                    for line in payload.extractfile('SHA256SUMS').read().decode().splitlines():
                        expected,name=line.split('  ',1)
                        self.assertEqual(hashlib.sha256(payload.extractfile(name).read()).hexdigest(),expected)
                    self.assertEqual(payload.getmember('cc2-control').mode,0o755)
                    self.assertEqual(payload.getmember('reactor/libcc2-reactor-v2.so').mode,0o700)
                    self.assertIn(hashlib.sha256(data).hexdigest().encode(),payload.extractfile('install-combined.sh').read())
                    info=json.loads(payload.extractfile('build-info.json').read())
                    self.assertEqual(info['source_archive_sha256'],hashlib.sha256(sources).hexdigest())
                    self.assertEqual(info['reactor_optimization'],'O0')
                with zipfile.ZipFile(io.BytesIO(sources)) as source:
                    self.assertIn('experimental/combined-test/reactor/reactor_module.cpp',source.namelist())
                    self.assertNotIn('experimental/combined-test/reactor/libcc2-reactor-v2.so',source.namelist())
if __name__=='__main__':unittest.main()
