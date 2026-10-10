"""Exercise component provenance, opt-in startup wiring and reject paths."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

BASE=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('canvas_component',BASE/'core/canvas_eject_component.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
SOURCE=BASE.parents[1]/'experimental/canvas-eject'
class CanvasComponentTests(unittest.TestCase):
    def elf_fixture(self,path,prefix=b'\0',address=0x100,code=b'12345678',value=0x100):
        strings=b'\0.shstrtab\0.text\0.dynstr\0.dynsym\0'
        names=prefix+b'function\0'
        symbols=bytes(16)+struct.pack('<IIIBBH',len(prefix),value,8,0x12,0,2)
        data=bytearray(52);data[:6]=b'\x7fELF\x01\x01'
        sections=[(0,)*10]
        for name,kind,flags,addr,body,link,entry in [
            (b'.shstrtab',3,0,0,strings,0,0),(b'.text',1,6,address,code,0,0),
            (b'.dynstr',3,2,0,names,0,0),(b'.dynsym',11,2,0,symbols,3,16)]:
            sections.append((strings.index(name),kind,flags,addr,len(data),len(body),link,0,1,entry));data.extend(body)
        offset=len(data)
        for section in sections:data.extend(struct.pack('<10I',*section))
        struct.pack_into('<I',data,32,offset);struct.pack_into('<HHH',data,46,40,len(sections),1)
        path.write_bytes(data);return m.executable_sections(path)
    def test_elf_code_and_symbol_abi(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'fixture.so';original=self.elf_fixture(path)
            self.assertEqual(original,self.elf_fixture(path,prefix=b'\0another-needed-string\0'))
            self.assertNotEqual(original,self.elf_fixture(path,address=0x200))
            self.assertNotEqual(original,self.elf_fixture(path,code=b'87654321'))
            self.assertNotEqual(original,self.elf_fixture(path,value=0x200))
    def fixture(self,path):
        component=path/'component';component.mkdir()
        elf=bytearray(52);elf[:6]=b'\x7fELF\x01\x01';struct.pack_into('<H',elf,16,3);struct.pack_into('<H',elf,18,40)
        files={}
        for name in m.NAMES:(component/name).write_bytes(elf);files[name]=m.digest(component/name)
        manifest={'gcc':'6.5.0','files':files,'sources':{name:m.digest(SOURCE/name) for name in ('runtime.cpp','screen.cpp','eject.h','hook.h','qualified.h')}}
        (component/'manifest.json').write_text(json.dumps(manifest));return component
    def test_provenance_and_corruption(self):
        with tempfile.TemporaryDirectory() as tmp:
            component=self.fixture(Path(tmp));m.load(component)
            (component/m.NAMES[0]).write_bytes(b'bad')
            with self.assertRaises(RuntimeError):m.load(component)
    def test_stale_source_and_compiler(self):
        with tempfile.TemporaryDirectory() as tmp:
            component=self.fixture(Path(tmp));p=component/'manifest.json';record=json.loads(p.read_text())
            record['sources']['runtime.cpp']='0'*64;p.write_text(json.dumps(record))
            with self.assertRaises(RuntimeError):m.load(component)
            record['gcc']='11';p.write_text(json.dumps(record))
            with self.assertRaises(RuntimeError):m.load(component)
    def test_gui_wiring_preserves_redirection(self):
        original='start(){\n    ec-eeb001-gui > /dev/null 2>&1 &\n    sleep 8\n    run_printer.sh start &\n}\n'
        result=m.patched_init(original)
        self.assertIn('/launch-screen.sh > /dev/null 2>&1 &',result)
        self.assertIn('sleep 8\n    run_printer.sh start &',result)
        for bad in ['none',original+original,result]:
            with self.assertRaises(RuntimeError):m.patched_init(bad)
    def test_target_mismatch_before_mutation(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);component=self.fixture(path);root=path/'root';(root/'opt/lib').mkdir(parents=True);(root/'opt/bin').mkdir()
            extras=root/'opt/lib/libelegoo_extras.so';extras.write_bytes(b'unknown')
            (root/'opt/bin/ec-eeb001-gui').write_bytes(b'unknown')
            with patch.object(subprocess,'run') as run:
                with self.assertRaises(RuntimeError):m.install(root,component)
                run.assert_not_called()
            self.assertEqual(extras.read_bytes(),b'unknown')
    def test_state_machine_and_screen_events(self):
        with tempfile.TemporaryDirectory() as tmp:
            for name in ('test_eject.cpp','test_screen.cpp'):
                binary=Path(tmp)/name[:-4]
                subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror',str(SOURCE/name),'-o',str(binary)],check=True)
                subprocess.run([str(binary)],cwd=tmp,check=True)
if __name__=='__main__':unittest.main()
