#!/usr/bin/env python3
from pathlib import Path
import hashlib
import shutil
import sys

STOCK_SHA256 = "b760fd80bb03de28ac348756a9a1c5311066377876570d2fdfd45fb562d91cdb"
PAYLOAD_SHA256 = "33d013ca4a63334fdbbd0adb77bd10f81e2e21c3ac41229e216eb17e5949d06f"
FINAL_SHA256 = "3af6104d0ac76cc043ecf38985e1b00a0d5ceace0a4f4b66820e21f4735a98c7"

HOOK_OFFSET = 0x2C68C
INJECT_OFFSET = 0x852AC
PAYLOAD_SIZE = 0x318

ELF_FILESZ_OFFSET = 0xA4
ELF_MEMSZ_OFFSET = 0xA8
OLD_SEGMENT_SIZE = 0x852AC
NEW_SEGMENT_SIZE = 0x855C4

STOCK_HOOK = bytes.fromhex("4f fb ff eb")
DUAL_HOOK = bytes.fromhex("06 63 01 eb")

HERE = Path(__file__).resolve().parent
PAYLOAD = HERE / "dual_verify.bin"

def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()

def sha256_file(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()

def fail(msg):
    raise RuntimeError(msg)

def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} INPUT_DAEMON OUTPUT_DAEMON")
        return 2

    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])

    if dst.exists() or dst.is_symlink():
        fail("Output already exists; choose a new path")

    if not src.is_file():
        fail(f"Input daemon not found: {src}")

    if not PAYLOAD.is_file():
        fail(f"Dual Trust payload not found: {PAYLOAD}")

    print("=== CC2 DUAL TRUST PATCHER ===")
    print(f"Input : {src}")
    print(f"Output: {dst}")

    stock_hash = sha256_file(src)
    print(f"Stock SHA256   : {stock_hash}")

    if stock_hash != STOCK_SHA256:
        fail("Input daemon SHA256 does not match frozen CC2 02.01.00.00 stock daemon")

    payload = PAYLOAD.read_bytes()

    print(f"Payload size   : {len(payload)}")
    print(f"Payload SHA256 : {sha256_bytes(payload)}")

    if len(payload) != PAYLOAD_SIZE:
        fail(f"Unexpected payload size: {len(payload)} != {PAYLOAD_SIZE}")

    if sha256_bytes(payload) != PAYLOAD_SHA256:
        fail("Dual Trust payload SHA256 mismatch")

    data = bytearray(src.read_bytes())

    if data[HOOK_OFFSET:HOOK_OFFSET + 4] != STOCK_HOOK:
        fail("Original hook instruction mismatch")

    cave = data[INJECT_OFFSET:INJECT_OFFSET + PAYLOAD_SIZE]

    if len(cave) != PAYLOAD_SIZE:
        fail("Injection area extends beyond daemon")

    if any(cave):
        fail("Injection code cave is not empty")

    print(f"Hook offset    : 0x{HOOK_OFFSET:X}")
    print(f"Inject offset  : 0x{INJECT_OFFSET:X}")
    print(f"Original hook  : {STOCK_HOOK.hex(' ')}")
    print(f"Patched hook   : {DUAL_HOOK.hex(' ')}")

    # Extend the first executable PT_LOAD segment so that the
    # injected Dual Trust payload is mapped R+X by the ELF loader.
    filesz = int.from_bytes(
        data[ELF_FILESZ_OFFSET:ELF_FILESZ_OFFSET + 4], "little"
    )
    memsz = int.from_bytes(
        data[ELF_MEMSZ_OFFSET:ELF_MEMSZ_OFFSET + 4], "little"
    )

    print(f"ELF p_filesz   : 0x{filesz:X} -> 0x{NEW_SEGMENT_SIZE:X}")
    print(f"ELF p_memsz    : 0x{memsz:X} -> 0x{NEW_SEGMENT_SIZE:X}")

    if filesz != OLD_SEGMENT_SIZE:
        fail(f"Unexpected ELF p_filesz: 0x{filesz:X}")

    if memsz != OLD_SEGMENT_SIZE:
        fail(f"Unexpected ELF p_memsz: 0x{memsz:X}")

    data[ELF_FILESZ_OFFSET:ELF_FILESZ_OFFSET + 4] = (
        NEW_SEGMENT_SIZE.to_bytes(4, "little")
    )
    data[ELF_MEMSZ_OFFSET:ELF_MEMSZ_OFFSET + 4] = (
        NEW_SEGMENT_SIZE.to_bytes(4, "little")
    )

    if int.from_bytes(data[0xC8:0xCC], "little") != 0x1EB0:
        fail("Unexpected RW memory size")
    data[0xC8:0xCC] = (0x1EF4).to_bytes(4, "little")
    data[INJECT_OFFSET:INJECT_OFFSET + PAYLOAD_SIZE] = payload
    data[HOOK_OFFSET:HOOK_OFFSET + 4] = DUAL_HOOK

    result_hash = sha256_bytes(data)

    print(f"Result SHA256  : {result_hash}")

    if result_hash != FINAL_SHA256:
        fail("Patched daemon does not match frozen Dual Trust v2 reference")

    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(data)
    shutil.copymode(src, dst)

    print()
    print("[OK] Stock daemon verified")
    print("[OK] Original hook verified")
    print("[OK] Empty injection area verified")
    print("[OK] Dual Trust payload verified")
    print("[OK] Hook patched")
    print("[OK] Dual Trust injected")
    print("[OK] Final daemon SHA256 matches reference")
    print()
    print("DUAL TRUST PATCH: PASS")

    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
