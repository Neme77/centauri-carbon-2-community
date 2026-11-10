"""The 0113 patch set reproduces the base profile's output pins.

Objective: from the 02.01.01.13 profile's stock pins, apply_patches.py must
produce exactly the profile's port.outputs hashes (the hardware- and
emulation-validated patched binaries). The pinned port scripts are verified
too. Skips when the local 0113 stock/patched inputs (gitignored WORK/) are
absent, e.g. on a fresh clone or CI.
"""
import hashlib
import importlib.util
import sys
import tempfile
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('builder_port0113', BASE / 'core/firmware_builder.py')
b = importlib.util.module_from_spec(spec)
spec.loader.exec_module(b)

profile = b.load_base_profile('02.01.01.13')
pins = profile['sha256']
out = profile['port']['outputs']

# The 0113 port scripts are hash-pinned; this must hold even without WORK/.
b.validate_port_0113_inputs()

WORK = BASE.parents[1] / 'experimental/firmware-port-0113/WORK'
stock_printer = WORK / 'elegoo_printer.stock'
patched_gui = WORK / 'corrected_v2/ec-eeb001-gui.patched'
patched_camera = WORK / 'corrected_v2/ai_camera.patched'
if not (stock_printer.exists() and patched_gui.exists() and patched_camera.exists()):
    print('SKIP test_port_0113: local 0113 inputs (WORK/) not present')
    sys.exit(0)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def reconstruct_stock(patched, offset, before_hex):
    """Undo a single-word patch to recover the stock binary from the patched one."""
    data = bytearray(patched.read_bytes())
    data[offset:offset + 4] = bytes.fromhex(before_hex)
    return bytes(data)


# GUI/camera are single-instruction patches (apply_patches.py GUI_PATCH/CAMERA_PATCH).
stock_gui = reconstruct_stock(patched_gui, 0x647C8, '9ce700eb')
stock_camera = reconstruct_stock(patched_camera, 0x12287C, 'b1df00eb')

assert sha(stock_printer.read_bytes()) == pins['stock_printer'], 'stock printer != profile pin'
assert sha(stock_gui) == pins['stock_gui'], 'reconstructed stock GUI != profile pin'
assert sha(stock_camera) == pins['stock_ai_camera'], 'reconstructed stock camera != profile pin'

with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    sp = td / 'elegoo_printer'; sp.write_bytes(stock_printer.read_bytes())
    sg = td / 'ec-eeb001-gui'; sg.write_bytes(stock_gui)
    sc = td / 'ai_camera'; sc.write_bytes(stock_camera)
    printer_out, gui_out, camera_out = b.apply_patches_0113(sp, sg, sc, td / 'out')
    assert sha(printer_out.read_bytes()) == out['printer_release']['sha256'], 'printer output != profile pin'
    assert sha(gui_out.read_bytes()) == out['patched_gui'], 'GUI output != profile pin'
    assert sha(camera_out.read_bytes()) == out['camera_release'], 'camera output != profile pin'

print('PASS: 0113 patch set reproduces the profile output pins from the profile stock pins')
