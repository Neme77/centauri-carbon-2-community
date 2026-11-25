"""Package the exact physically tested payload, without committing vendor binaries."""
from pathlib import Path
import argparse, hashlib, io, json, stat, tarfile, zipfile

H = Path(__file__).resolve().parent
BUNDLE_SHA = "034f420bfff43632367d2b5ca7cc919f730486675f9bd7bc8611ecc87d19f697"
GUI_SHA = "b492eed34b900ad4a71d45df89d5ed0c410990c7c53c79424dfff656f892f926"
LIB_SHA = "0b09964c1f3dadddc0c80b53389601f9c26268a99af297caa1741622e55ba3a6"
NAME = "CC2-Original-Display-Style-1.2.30"

def digest(data): return hashlib.sha256(data).hexdigest()

def build(bundle, output):
    data = bundle.read_bytes()
    if digest(data) != BUNDLE_SHA: raise ValueError("Unqualified payload archive")
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
        members = tar.getmembers()
        names = [m.name for m in members]
        if len(names) != 436 or len(set(names)) != len(names): raise ValueError("Unexpected archive inventory")
        for member in members:
            if not member.isfile() or member.name.startswith("/") or ".." in Path(member.name).parts:
                raise ValueError("Unsafe archive entry")
        if tar.extractfile("install.sh").read() != (H / "install.sh").read_bytes():
            raise ValueError("Installer source differs from tested payload")
        for name, expected in [("payload/gui", GUI_SHA), ("payload/library", LIB_SHA)]:
            if digest(tar.extractfile(name).read()) != expected: raise ValueError(name)
        rows = tar.extractfile("files.txt").read().decode("ascii").splitlines()
        if len(rows) != 862: raise ValueError("Target count differs")
        for row in rows:
            rel, old, new, mode, source = row.split()
            if digest(tar.extractfile("payload/" + source).read()) != new: raise ValueError(rel)
    output.mkdir(parents=True, exist_ok=True)
    files = {"display-kit.tar.gz": data, "Printer.ps1": (H / "Printer.ps1").read_bytes(),
             "printer.py": (H / "printer.py").read_bytes(),
             "README-FIRST.md": (H.parents[1] / "docs/ORIGINAL_DISPLAY_STYLE.md").read_bytes(),
             "qualification.json": (H / "qualification.json").read_bytes()}
    for action in ("Check", "Install", "Restore", "Status"):
        files[action + ".cmd"] = ("@echo off\r\nsetlocal\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File \"%~dp0Printer.ps1\" -Action " + action + "\r\nif errorlevel 1 echo STOPPED. Keep report and kit; do not retry automatically.\r\npause\r\n").replace('\\"','"').encode("ascii")
        files[action + ".sh"] = ('#!/bin/sh\nset -eu\ncd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"\nexec python3 ./printer.py ' + action + '\n').encode("ascii")
    files["SHA256SUMS.txt"] = ''.join(digest(value) + "  " + key + "\n" for key, value in sorted(files.items())).encode("ascii")
    target = output / (NAME + ".zip")
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, value in sorted(files.items()):
            info = zipfile.ZipInfo(NAME + "/" + name, date_time=(2026, 11, 25, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | (0o755 if name.endswith(".sh") else 0o644)) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, value)
    checksum = digest(target.read_bytes())
    target.with_suffix(".zip.sha256.txt").write_text(checksum + "  " + target.name + "\n", encoding="ascii")
    print("MANUAL_STYLE_PACKAGE_VERIFIED " + checksum)
    return target

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build(args.bundle, args.output)
