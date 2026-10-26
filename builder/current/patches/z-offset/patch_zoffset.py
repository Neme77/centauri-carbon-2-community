from pathlib import Path
import hashlib
import shutil
import sys

STOCK_SHA256 = "a231c26bc965b0e2ee5edbf4fc1ca4018fed009da601a7e7dc267a0c81e7fb08"
PATCHED_SHA256 = "afa2b1f181d18dc4803a60ee61fb3fc454f1e91afa12743bc1a1e5e386e439ff"

OFFSET = 0x63C5C

ORIGINAL = bytes.fromhex("63 e7 00 eb")
PATCHED  = bytes.fromhex("00 01 00 ea")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def fail(message):
    print(f"ERROR: {message}", file=sys.stderr)
    sys.exit(1)


if len(sys.argv) != 3:
    print(f"Usage: {sys.argv[0]} STOCK_GUI OUTPUT_GUI")
    sys.exit(2)

src = Path(sys.argv[1])
dst = Path(sys.argv[2])

if not src.is_file():
    fail(f"input file does not exist: {src}")

digest = sha256(src)

print("Input SHA256 :", digest)

if digest != STOCK_SHA256:
    fail("unsupported ec-eeb001-gui binary; stock SHA256 does not match")

with src.open("rb") as f:
    f.seek(OFFSET)
    current = f.read(len(ORIGINAL))

print("Patch offset :", hex(OFFSET))
print("Expected     :", ORIGINAL.hex(" "))
print("Found        :", current.hex(" "))

if current != ORIGINAL:
    fail("original instruction bytes do not match")

if dst.exists():
    fail(f"output already exists: {dst}")

shutil.copyfile(src, dst)

with dst.open("r+b") as f:
    f.seek(OFFSET)
    f.write(PATCHED)
    f.flush()

result = sha256(dst)

print("Patched bytes:", PATCHED.hex(" "))
print("Output SHA256:", result)

if result != PATCHED_SHA256:
    dst.unlink(missing_ok=True)
    fail("patched output SHA256 does not match known-good binary")

print()
print("Z-offset patch applied successfully.")
