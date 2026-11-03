"""Apply the reviewed presentation patch to an owner-supplied native ELF.

Never loads or executes the vendor program. Unknown inputs are refused.
"""
from pathlib import Path
import argparse
import hashlib
import json

from elf_layout import expand_rx

HERE = Path(__file__).resolve().parent


def sha(data):
    return hashlib.sha256(data).hexdigest()


def apply(data, plan=None):
    if plan is None:
        plan = json.loads((HERE / "patch-plan.json").read_text())
    if len(data) != plan["base_size"] or sha(data) != plan["base_sha256"]:
        raise ValueError("Unsupported native display executable; no output written")
    result = bytearray(expand_rx(data, b"\0" * 4))
    if sha(result) != plan["expanded_sha256"]:
        raise ValueError("Unexpected ELF layout expansion")
    previous_end = 0
    for entry in plan["changes"]:
        start, size = entry["offset"], entry["length"]
        replacement = bytes.fromhex(entry["data"])
        if (start < previous_end or size <= 0 or start + size > len(result)
                or len(replacement) != size
                or sha(result[start:start + size]) != entry["before_sha256"]):
            raise ValueError("Overlapping, invalid or changed patch span")
        result[start:start + size] = replacement
        previous_end = start + size
    if len(result) != plan["output_size"] or sha(result) != plan["output_sha256"]:
        raise ValueError("Patched display does not match the qualified output")
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Output already exists; retain previous artifacts")
    result = apply(args.native.read_bytes())
    with args.output.open("xb") as file:
        file.write(result)
    print("PATCH VERIFIED:", sha(result), "No executable run.")


if __name__ == "__main__":
    main()
