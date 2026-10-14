"""Reproduce the 800 ARM comparison cases for the recognized 02.01 library."""
import argparse
import hashlib
import json
from pathlib import Path
import random
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
from verify_arm import emulate

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--library', required=True, type=Path)
args = parser.parse_args()
raw = args.library.read_bytes()
manifest = json.loads(Path(__file__).with_name('manifest.json').read_text())
x = next(v for v in manifest['libraries'] if v['ota_version'] == '02.01.00.00')
if hashlib.sha256(raw).hexdigest() != x['input_sha256']:
    parser.error('library does not match the recognized 02.01.00.00 original')
off = x['file_offset']
assert raw[off:off+24].hex() == x['original_hex']
assert raw[off-16:off].hex() == 'c76bb0ee24311be5903a07eee77bb8ee'
old = raw[off-16:off+28]
new = old[:16] + bytes.fromhex(x['patched_hex']) + old[40:]
start = x['virtual_address'] - 16
instructions = list(Cs(CS_ARCH_ARM, CS_MODE_ARM).disasm(old, start))
loop_target = int(instructions[-1].op_str.lstrip('#'), 16)
assert loop_target == 0xcb9740
assert emulate(old, start, -10, 0) == (1, start+44)
rng = random.Random(2010000)
distances = [-1400, -600, -10, -1, 1, 10, 600, 1400]
distances += [rng.choice([-1, 1]) * rng.randint(1, 1400) for _ in range(72)]
count = 0
for distance in distances:
    target = abs(distance)
    for magnitude in (0, target/2, target, target+0.25, target*1.1):
        for sign in (-1, 1):
            expected = int(abs(magnitude*sign) > target)
            got = emulate(new, start, distance, magnitude*sign)
            assert got == (expected, start+44 if expected else loop_target)
            if distance > 0:
                assert got == emulate(old, start, distance, magnitude*sign)
            count += 1
print(json.dumps({'version': '02.01.00.00', 'cases_passed': count,
                  'original_bug_reproduced': True, 'forward_behaviour_preserved': True,
                  'branch_destinations_verified': True, 'hardware_tested': False}))
