"""Build and apply the authored thumbnail patch to the exact .07 Community GUI."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SOURCE = HERE / "thumbnail.c"
PLAN = HERE / "thumbnail07.json"
INPUT_SHA256 = "db09fa37d378df259427aaac392a341282e08ceb26cd87603542c0dcbcbb91d1"
RX_HEADER = 52 + 3 * 32
RX = (1, 0, 0x10000, 0x10000, 0x214D88, 0x214D88, 5, 0x10000)
RW = (1, 0x2207C0, 0x2307C0, 0x2307C0, 0x24B3C, 0x11062B0, 6, 0x10000)
PAYLOAD_ADDR = RX[2] + RX[4]

# A non-linking branch replaces the extractor's entry. The other branches
# retain the original call sites and affect only embedded G-code previews.
HOOKS = {
    0x4C938: (0xE92D47F0, 0xEA000000, "cc2_extract_thumbnail"),
    0x50460: (0xEB0341D8, 0xEB000000, "cc2_set_thumbnail_src"),
    0x5051C: (0xEB0341A9, 0xEB000000, "cc2_set_thumbnail_src"),
    0x50680: (0xEB03455E, 0xEB000000, "cc2_set_thumbnail_zoom"),
    0x50764: (0xEB034117, 0xEB000000, "cc2_set_thumbnail_src"),
    0x50AD8: (0xEB03403A, 0xEB000000, "cc2_set_thumbnail_src"),
    0x51D78: (0xEB033B92, 0xEB000000, "cc2_set_thumbnail_src"),  # Cached image on the file-list tile.
}
ENTRIES = {name for _, _, name in HOOKS.values()}

# Import addresses are the native GUI's ARM PLT entries or LVGL functions.
IMPORTS = {
    "free": 0x12908,
    "fopen": 0x12B84,
    "__isoc99_sscanf": 0x12C74,
    "fgets": 0x12EB4,
    "memcpy": 0x12EF0,
    "malloc": 0x12F20,
    "strlen": 0x12F2C,
    "fclose": 0x12F80,
    "strcmp": 0x13004,
    "memcmp": 0x13088,
    "strchr": 0x13130,
    "lv_obj_get_self_width": 0xF69F0,
    "lv_obj_get_self_height": 0xF6A24,
    "lv_img_set_src": 0x120BC8,
    "lv_img_set_pivot": 0x121AA4,
    "lv_img_set_zoom": 0x121C00,
    "lv_img_set_size_mode": 0x121D90,
    "lv_img_get_zoom": 0x121DD8,
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def insert(raw, payload, entries):
    if sha(raw) != INPUT_SHA256:
        raise ValueError("Unsupported native GUI for the thumbnail patch")
    if (struct.unpack_from("<8I", raw, RX_HEADER) != RX or
            struct.unpack_from("<8I", raw, RX_HEADER + 32) != RW):
        raise ValueError("Unexpected native GUI program headers")
    stop = PAYLOAD_ADDR + len(payload)
    if (not payload or stop > (RW[2] & ~0xFFF) or
            any(raw[RX[4]:RX[4] + len(payload)])):
        raise ValueError("Thumbnail payload does not fit in the reserved RX gap")
    if set(entries) != ENTRIES or any(address % 4 or not PAYLOAD_ADDR <= address < stop
                                      for address in entries.values()):
        raise ValueError("Invalid thumbnail payload entry points")

    result = bytearray(raw)
    for site, (original, opcode, name) in HOOKS.items():
        offset = site - RX[2]
        if struct.unpack_from("<I", raw, offset)[0] != original:
            raise ValueError(f"Unexpected native thumbnail instruction at {site:#x}")
        displacement = (entries[name] - (site + 8)) // 4
        if not -(1 << 23) <= displacement < (1 << 23):
            raise ValueError("Thumbnail payload branch is out of range")
        struct.pack_into("<I", result, offset, opcode | (displacement & 0xFFFFFF))
    end = RX[4] + len(payload)
    result[RX[4]:end] = payload
    struct.pack_into("<II", result, RX_HEADER + 16, end, end)
    return bytes(result)


def apply(raw):
    plan = json.loads(PLAN.read_text(encoding="utf-8"))
    if sha(SOURCE.read_bytes()) != plan["source_sha256"]:
        raise ValueError("Thumbnail source differs from the reviewed payload")
    result = insert(raw, bytes.fromhex(plan["payload"]), plan["entries"])
    if sha(result) != plan["output_sha256"]:
        raise ValueError("Native GUI with thumbnail patch differs from the reviewed output")
    return result


def build_plan(raw, cross):
    if sha(raw) != INPUT_SHA256:
        raise ValueError("Unsupported native GUI for the thumbnail patch")
    with tempfile.TemporaryDirectory(prefix="cc2-thumbnail-") as temporary:
        directory = Path(temporary)
        layout = directory / "payload.ld"
        layout.write_text(
            f"SECTIONS {{ .payload {PAYLOAD_ADDR:#x} : {{ *(.text .text.* .rodata .rodata.*) }}\n"
            "  .data : { *(.data .data.* .bss .bss.* .got .got.*) }\n"
            '  ASSERT(SIZEOF(.data) == 0, "Thumbnail payload must not own writable state")\n'
            "  /DISCARD/ : { *(.ARM.exidx* .ARM.extab* .comment .note*) } }\n",
            encoding="ascii")
        compiled = directory / "payload.elf"
        payload_path = directory / "payload.bin"
        symbols = [f"-Wl,--defsym,{name}={addr:#x}" for name, addr in IMPORTS.items()]
        subprocess.run([cross + "gcc", "-std=c99", "-Os", "-marm", "-mcpu=cortex-a7",
                        "-mfpu=neon-vfpv4", "-mfloat-abi=hard", "-Wall", "-Wextra", "-Werror",
                        "-U_FORTIFY_SOURCE", "-U_FILE_OFFSET_BITS", "-U_TIME_BITS",
                        "-fno-builtin", "-fno-stack-protector", "-fno-pic", "-fno-pie",
                        "-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-nostdlib",
                        "-static", "-no-pie", "-Wl,--build-id=none", "-T", str(layout),
                        *symbols, str(SOURCE), "-o", str(compiled)], check=True, timeout=60)
        table = subprocess.check_output([cross + "nm", "--defined-only", str(compiled)],
                                        text=True, timeout=30)
        entries = {}
        for row in table.splitlines():
            address, _, name = row.split()
            if name in ENTRIES:
                entries[name] = int(address, 16)
        subprocess.run([cross + "objcopy", "-O", "binary", "-j", ".payload",
                        str(compiled), str(payload_path)], check=True, timeout=30)
        payload = payload_path.read_bytes()
    return {
        "source_sha256": sha(SOURCE.read_bytes()),
        "output_sha256": sha(insert(raw, payload, entries)),
        "entries": entries,
        "payload": payload.hex(),
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("gui", type=Path, help="Exact .07 GUI after the Community display patch")
    parser.add_argument("output", type=Path, help="New output ELF, or new plan JSON with --build-plan")
    parser.add_argument("--build-plan", action="store_true")
    parser.add_argument("--cross", default="arm-linux-gnueabihf-")
    args = parser.parse_args()
    original = args.gui.read_bytes()
    if args.build_plan:
        data = (json.dumps(build_plan(original, args.cross), indent=2) + "\n").encode("ascii")
    else:
        data = apply(original)
    with args.output.open("xb") as output:
        output.write(data)
    print("VERIFIED native thumbnail", sha(data))
