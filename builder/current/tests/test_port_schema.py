"""Host checks for the per-base port contract.

Every base profile carries the same port keys. Output pins follow the
components section (native_gui only when native display is included), and a
profile that drops or adds a port key is refused. The 07 release pins stay a
frozen copy of the module constants, and the build path reads those pins from
the profile rather than from a second per-base global.
"""
import ast
import json
import re
import tempfile
import unittest
from pathlib import Path
import importlib.util

BASE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('builder_port_schema', BASE / 'core/firmware_builder.py')
b = importlib.util.module_from_spec(spec)
spec.loader.exec_module(b)

_VERSION = re.compile(r'\d{2}\.\d{2}\.\d{2}\.\d{2}')
_FROZEN_RELEASES = {
    'PRINTER_RELEASE_SHA256',
    'PRINTER_RELEASE_MD5',
    'PRINTER_RELEASE_SIZE',
    'PRINTER_RELEASE_DEFAULT',
    'CAMERA_RELEASE_SHA256',
}


def _slice_value(node):
    sl = node.slice
    if isinstance(sl, ast.Index):  # Python 3.8 and older
        sl = sl.value
    if isinstance(sl, ast.Constant):
        return sl.value
    if isinstance(sl, ast.Str):
        return sl.s
    return None


def _subscript_chain(node):
    chain = []
    while isinstance(node, ast.Subscript):
        chain.append(_slice_value(node))
        node = node.value
    if isinstance(node, ast.Name):
        chain.append(node.id)
    else:
        return None
    chain.reverse()
    return chain


def _unqualified_copy():
    """0113's port shape under a version that is not in QUALIFIED_BASES.

    The qualified 07 file is hash-pinned before the port schema runs, so a
    schema rejection has to be shown on a profile the hash gate does not own.
    """
    data = json.loads((BASE / 'base_profiles/02.01.01.13.json').read_text())
    data['version'] = '02.01.01.99'
    data['manifest']['version'] = '02.01.01.99'
    data['embedded_filename_marker'] = '02.01.01.99'
    return data


class PortSchemaTests(unittest.TestCase):
    def _load(self, data):
        with tempfile.TemporaryDirectory() as td:
            directory = Path(td)
            (directory / '02.01.01.99.json').write_text(json.dumps(data))
            return b.load_base_profile('02.01.01.99', directory)

    def _rejected(self, data, text):
        with self.assertRaises(RuntimeError) as caught:
            self._load(data)
        self.assertIn(text, str(caught.exception))

    def test_each_profile_has_the_port_contract(self):
        paths = sorted((BASE / 'base_profiles').glob('*.json'))
        self.assertGreaterEqual(len(paths), 2)
        seen = set()
        for path in paths:
            profile = b.load_base_profile(path.stem)
            seen.add(profile['version'])
            self.assertEqual(set(profile['port']), set(b.PORT_KEYS), path.name)
            outputs = {k for k in profile['port']['outputs'] if not k.startswith('_')}
            expected = set(b.PORT_OUTPUT_KEYS)
            if b.component_included(profile, 'native_display'):
                expected.add(b.PORT_NATIVE_OUTPUT)
            self.assertEqual(outputs, expected, path.name)
            release = profile['port']['outputs']['printer_release']
            self.assertEqual(set(release), {'sha256', 'md5', 'size'})
            for key in outputs - {'printer_release'}:
                self.assertRegex(profile['port']['outputs'][key], r'^[0-9a-f]{64}$')
            self.assertRegex(release['sha256'], r'^[0-9a-f]{64}$')
            self.assertRegex(release['md5'], r'^[0-9a-f]{32}$')
            self.assertIsInstance(release['size'], int)
            self.assertGreater(release['size'], 0)
        self.assertEqual(seen, {'02.01.01.07', '02.01.01.13'})
        qualified = b.load_base_profile('02.01.01.07')
        self.assertIn('native_gui', qualified['port']['outputs'])
        candidate = b.load_base_profile('02.01.01.13')
        self.assertNotIn('native_gui', {k for k in candidate['port']['outputs'] if not k.startswith('_')})
        self.assertFalse(b.component_included(candidate, 'native_display'))

    def test_port_key_added_or_removed_is_refused(self):
        added = _unqualified_copy()
        added['port']['note'] = 'extra'
        self._rejected(added, 'port must have exactly')
        removed = _unqualified_copy()
        del removed['port']['golden']
        self._rejected(removed, 'port must have exactly')
        # The same shape without the mutation still loads. 02.01.01.99 is not
        # a qualified base, so this does not open the build gate.
        loaded = self._load(_unqualified_copy())
        self.assertEqual(loaded['version'], '02.01.01.99')
        self.assertFalse(loaded['ported_patch_set'])

    def test_output_pins_follow_native_display_and_hash_form(self):
        missing = _unqualified_copy()
        del missing['port']['outputs']['camera_init']
        self._rejected(missing, 'port.outputs must have exactly')
        extra = _unqualified_copy()
        extra['port']['outputs']['native_gui'] = 'a' * 64
        self._rejected(extra, 'port.outputs must have exactly')
        included = _unqualified_copy()
        del included['components']
        self._rejected(included, 'native_gui')
        bad_hash = _unqualified_copy()
        bad_hash['port']['outputs']['camera_release'] = 'A' * 64
        self._rejected(bad_hash, 'Invalid SHA256 pin port.outputs.camera_release')
        bad_release = _unqualified_copy()
        bad_release['port']['outputs']['printer_release']['md5'] = 'zz' * 16
        self._rejected(bad_release, 'Invalid MD5 pin')
        bad_size = _unqualified_copy()
        bad_size['port']['outputs']['printer_release']['size'] = 0
        self._rejected(bad_size, 'printer_release.size')
        unknown_patch = _unqualified_copy()
        unknown_patch['port']['printer_patch'] = 'other'
        self._rejected(unknown_patch, 'Unknown port.printer_patch')

    def test_07_release_pins_are_the_frozen_profile_copy(self):
        outputs = b.load_base_profile('02.01.01.07')['port']['outputs']
        self.assertEqual(outputs['printer_release']['sha256'], b.PRINTER_RELEASE_SHA256)
        self.assertEqual(outputs['printer_release']['md5'], b.PRINTER_RELEASE_MD5)
        self.assertEqual(outputs['printer_release']['size'], b.PRINTER_RELEASE_SIZE)
        self.assertEqual(outputs['camera_release'], b.CAMERA_RELEASE_SHA256)
        # 0113 does not reuse those 07 constants.
        other = b.load_base_profile('02.01.01.13')['port']['outputs']
        self.assertNotEqual(other['printer_release']['sha256'], b.PRINTER_RELEASE_SHA256)
        self.assertNotEqual(other['camera_release'], b.CAMERA_RELEASE_SHA256)

    def test_build_reads_release_pins_from_the_profile(self):
        source = (BASE / 'core/firmware_builder.py').read_text()
        tree = ast.parse(source)
        names = []
        for node in tree.body:
            if not isinstance(node, ast.Assign):
                continue
            for target in node.targets:
                if not isinstance(target, ast.Name):
                    continue
                names.append(target.id)
                if re.search(r'(PRINTER_RELEASE|CAMERA_RELEASE)', target.id):
                    self.assertIn(target.id, _FROZEN_RELEASES, target.id)
                if isinstance(node.value, ast.Dict) and re.search(r'(RELEASE|OUTPUT)', target.id):
                    for key in node.value.keys:
                        value = key.value if isinstance(key, ast.Constant) else getattr(key, 's', None)
                        self.assertFalse(isinstance(value, str) and _VERSION.fullmatch(value), target.id)
        self.assertIn('PRINTER_RELEASE_SHA256', names)
        self.assertIn('CAMERA_RELEASE_SHA256', names)
        main = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'main')
        used = {node.id for node in ast.walk(main) if isinstance(node, ast.Name)}
        self.assertNotIn('PRINTER_RELEASE_SHA256', used)
        self.assertNotIn('CAMERA_RELEASE_SHA256', used)
        assigned = {}
        for node in ast.walk(main):
            if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
                assigned[node.targets[0].id] = node.value
        self.assertEqual(_subscript_chain(assigned['printer_release_sha']), ['port_outputs', 'printer_release', 'sha256'])
        self.assertEqual(_subscript_chain(assigned['camera_release_sha']), ['port_outputs', 'camera_release'])
        self.assertEqual(_subscript_chain(assigned['camera_init_sha']), ['port_outputs', 'camera_init'])

    def test_component_exclusion_needs_a_reason(self):
        self.assertTrue(b.component_included({}, 'native_display'))
        self.assertTrue(b.component_included({'components': {'native_display': {'included': True}}}, 'native_display'))
        excluded = {'components': {'native_display': {'included': False, 'reason': 'not ported'}}}
        self.assertFalse(b.component_included(excluded, 'native_display'))
        self.assertTrue(b.component_included(excluded, 'camera'))
        for bad in (
            {'included': False, 'reason': '   '},
            {'included': False},
            {'included': 'no', 'reason': 'not a boolean'},
            'absent',
        ):
            with self.assertRaises(RuntimeError):
                b.component_included({'version': 'x', 'components': {'native_display': bad}}, 'native_display')


if __name__ == '__main__':
    unittest.main()
