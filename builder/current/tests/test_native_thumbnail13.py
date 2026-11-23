"""Synthetic .13 ELF insertion guards; no vendor binary or printer required."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SOURCE=Path(__file__).resolve().parents[3]/"experimental/native-display"
spec=importlib.util.spec_from_file_location("thumbnail13_guard_tests",SOURCE/"patch_thumbnail13.py")
port=importlib.util.module_from_spec(spec)
spec.loader.exec_module(port)

def synthetic_input():
    raw=bytearray(2755560)
    raw[:4]=b"\x7fELF"
    struct.pack_into("<8I",raw,148,*port.RX)
    struct.pack_into("<8I",raw,180,*port.RW)
    for site,(word,_opcode,_name) in port.HOOKS.items():
        struct.pack_into("<I",raw,site-65536,word)
    raw[200000:200016]=b"LANGUAGE_FIXTURE"
    return bytes(raw)

class Thumbnail13Guards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.plan=json.loads((SOURCE/"thumbnail13.json").read_text())
        cls.raw=synthetic_input()
        start=port.RX[2]+port.RX[4]
        cls.payload=b"\x00"*12
        cls.entries={"cc2_extract_thumbnail":start,"cc2_set_thumbnail_src":start+4,
                     "cc2_set_thumbnail_zoom":start+8}

    def test_reviewed_source_plan_and_import_provenance(self):
        proof=json.loads((SOURCE/"thumbnail13-proof.json").read_text())
        source_hash=hashlib.sha256((SOURCE/"thumbnail.c").read_bytes()).hexdigest()
        self.assertEqual(source_hash,self.plan["source_sha256"])
        self.assertEqual(source_hash,proof["source_sha256"])
        self.assertEqual(port.INPUT_SHA256,self.plan["input_sha256"])
        self.assertEqual(proof["output_sha256"],self.plan["output_sha256"])
        self.assertEqual({n:r["new"] for n,r in proof["imports"].items()},port.IMPORTS)
        self.assertEqual(self.plan["upstream_author"],"beanbo")
        self.assertFalse(proof["validation"]["physical_display_test"])

    def test_unknown_gui_is_refused(self):
        with self.assertRaises(ValueError):
            port.verify_input(self.raw)

    def test_exact_changes_and_branch_destinations(self):
        with patch.object(port,"INPUT_SHA256",port.sha(self.raw)):
            output=port.insert(self.raw,self.payload,self.entries)
        expected=bytearray(self.raw)
        expected[port.RX[4]:port.RX[4]+12]=self.payload
        struct.pack_into("<II",expected,164,port.RX[4]+12,port.RX[4]+12)
        for site,(_old,opcode,name) in port.HOOKS.items():
            word=opcode|(((self.entries[name]-site-8)//4)&0xffffff)
            struct.pack_into("<I",expected,site-65536,word)
            displacement=word&0xffffff
            if displacement&0x800000:displacement-=1<<24
            self.assertEqual(site+8+4*displacement,self.entries[name])
        self.assertEqual(output,bytes(expected))
        self.assertEqual(output[200000:200016],b"LANGUAGE_FIXTURE")

    def test_nonzero_gap_is_refused(self):
        raw=bytearray(self.raw);raw[port.RX[4]]=1
        with patch.object(port,"INPUT_SHA256",port.sha(raw)):
            with self.assertRaises(ValueError):
                port.insert(bytes(raw),self.payload,self.entries)

    def test_changed_program_header_is_refused(self):
        raw=bytearray(self.raw);raw[148]^=1
        with patch.object(port,"INPUT_SHA256",port.sha(raw)):
            with self.assertRaises(ValueError):port.verify_input(bytes(raw))

    def test_changed_native_hook_is_refused(self):
        raw=bytearray(self.raw);raw[0x50460-65536]^=1
        with patch.object(port,"INPUT_SHA256",port.sha(raw)):
            with self.assertRaises(ValueError):port.verify_input(bytes(raw))

    def test_invalid_payload_and_entry_points_are_refused(self):
        cases=[(b"",self.entries),
               (b"\0"*(port.RW[1]-port.RX[4]+1),self.entries),
               (self.payload,{}),
               (self.payload,dict(self.entries,cc2_extract_thumbnail=self.entries["cc2_extract_thumbnail"]+1)),
               (self.payload,dict(self.entries,cc2_extract_thumbnail=0x10000))]
        with patch.object(port,"INPUT_SHA256",port.sha(self.raw)):
            for payload,entries in cases:
                with self.subTest(length=len(payload),entries=entries):
                    with self.assertRaises(ValueError):port.insert(self.raw,payload,entries)

    def test_cli_refuses_unknown_input_without_creating_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            gui=Path(tmp)/"gui";output=Path(tmp)/"patched"
            gui.write_bytes(self.raw)
            result=subprocess.run([sys.executable,str(SOURCE/"patch_thumbnail13.py"),str(gui),str(output)],
                                  capture_output=True,timeout=30)
            self.assertNotEqual(result.returncode,0)
            self.assertFalse(output.exists())

if __name__=="__main__":
    unittest.main()
