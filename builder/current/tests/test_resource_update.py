from pathlib import Path
import importlib.util,hashlib,unittest
from unittest.mock import patch
FILE=Path(__file__).resolve().parents[1]/'core/resource_update.py'
spec=importlib.util.spec_from_file_location('native_resource_update',FILE)
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class ResourceUpdateTests(unittest.TestCase):
    def test_exact_two_selected_branches_only(self):
        data=b'branch A\n'+m.ANCHOR+b'keep A\nbranch B\n'+m.ANCHOR+b'keep B\n    bootenv: (\nkeep cleanup\n'
        with patch.object(m,'STOCK_SHA256',hashlib.sha256(data).hexdigest()):
            result=m.patch(data)
        self.assertEqual(result.replace(m.ANCHOR+m.ENTRY,m.ANCHOR),data)
        self.assertEqual(result.count(b'"ota_flag"'),2)
        self.assertTrue(result.endswith(b'    bootenv: (\nkeep cleanup\n'))

    def test_changed_description_refused(self):
        with self.assertRaisesRegex(RuntimeError,'Unsupported'):m.patch(b'foreign firmware')

    def test_ambiguous_branches_refused(self):
        for data in (m.ANCHOR,m.ANCHOR*3,m.ANCHOR*2+b'"ota_flag"'):
            with patch.object(m,'STOCK_SHA256',hashlib.sha256(data).hexdigest()):
                with self.assertRaises(RuntimeError):m.patch(data)

if __name__=='__main__':unittest.main()
