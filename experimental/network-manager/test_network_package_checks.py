import hashlib
import unittest
from network_package_checks import validate,bind_candidate

class Pins(unittest.TestCase):
    def test_correct_manifest_does_not_excuse_old_installer_pin(self):
        original=b'published binary';new=b'candidate binary'
        script=b'#!/bin/sh\nEXPECTED='+hashlib.sha256(original).hexdigest().encode()+b'\n# camera and compatibility checks\n'
        validate(script,original)
        with self.assertRaises(ValueError):validate(script,new)
        updated=bind_candidate(script,original,new)
        self.assertEqual(validate(updated,new),hashlib.sha256(new).hexdigest())
        self.assertEqual(updated.splitlines()[2:],script.splitlines()[2:])

    def test_duplicates_placeholders_and_unqualified_base_refused(self):
        original=b'original';script=b'EXPECTED='+hashlib.sha256(original).hexdigest().encode()+b'\n'
        for malformed in (script+script,b'EXPECTED=@BINARY_SHA256@\n',b'EXPECTED=wrong\n'):
            with self.assertRaises(ValueError):bind_candidate(malformed,original,b'new')
        with self.assertRaises(ValueError):bind_candidate(script,b'wrong base',b'new')

if __name__=='__main__':unittest.main()
