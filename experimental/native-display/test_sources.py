"""Public, vendor-free checks. These are not physical-printer acceptance."""
import hashlib
import json
from pathlib import Path
import re
import struct
import unittest

import patch
from png_palette import decode, encode, recolor

HERE = Path(__file__).resolve().parent


class Sources(unittest.TestCase):
    def test_refuses_foreign_executable(self):
        for data in (b"", b"ELF", b"x" * 2655456):
            with self.assertRaises(ValueError):
                patch.apply(data)

    def test_bounded_patch_and_authored_payload(self):
        plan = json.loads((HERE / "patch-plan.json").read_text())
        previous = total = 0
        for span in plan["changes"]:
            self.assertGreaterEqual(span["offset"], previous)
            self.assertEqual(len(bytes.fromhex(span["data"])), span["length"])
            previous = span["offset"] + span["length"]
            self.assertLessEqual(previous, plan["output_size"])
            total += span["length"]
        self.assertLess(total, 24000)
        words = re.findall(r"\.word 0x([0-9a-f]{8}) /\* address 0x([0-9a-f]{8})", (HERE / "display_payload.S").read_text())
        self.assertGreater(len(words), 3000)
        # The payload uses the original RX virtual-address/file offset delta.
        for word, address in words:
            offset = int(address, 16) - 0x10000
            value = struct.pack("<I", int(word, 16))
            matched = False
            for span in plan["changes"]:
                delta = offset - span["offset"]
                if 0 <= delta <= span["length"] - 4:
                    self.assertEqual(bytes.fromhex(span["data"])[delta:delta+4], value)
                    matched = True
                    break
            if not matched:
                self.assertEqual(value, b"\0" * 4)

    def test_authored_artwork_integrity(self):
        assets = json.loads((HERE / "assets/manifest.json").read_text())["assets"]
        self.assertEqual(len(assets), 430)
        for entry in assets:
            name = entry["name"]
            self.assertEqual(Path(name).name, name)
            self.assertNotIn("illustration", name)
            data = (HERE / "assets/png" / name).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), entry["sha256"])
            width, height, _ = decode(data)
            self.assertEqual((width, height), (entry["width"], entry["height"]))
            self.assertNotIn("<image", (HERE / "assets/svg" / Path(name).with_suffix(".svg").name).read_text().lower())

    def test_palette_preserves_alpha_and_other_colours(self):
        rgba = bytes((0, 220, 0, 128, 255, 0, 0, 255, 30, 40, 90, 0))
        data = encode(3, 1, rgba)
        expected = bytes((126, 234, 216, 128)) + rgba[4:]
        entry = dict(name="fixture.png", baseline_sha256=patch.sha(data), width=3,
                     height=1, expected_rgba_sha256=patch.sha(expected))
        self.assertEqual(decode(recolor(data, entry)), (3, 1, expected))
        with self.assertRaises(ValueError):
            recolor(data + b"foreign", entry)


if __name__ == "__main__":
    unittest.main()
