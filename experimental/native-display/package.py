"""Build a PRIVATE display component from exact locally owned firmware inputs."""
from pathlib import Path
import argparse
import gzip
import hashlib
import io
import json
import tarfile

from patch import apply, sha
from png_palette import recolor

HERE = Path(__file__).resolve().parent


def build_images(stock_images):
    files = {}
    owned = json.loads((HERE / "assets/manifest.json").read_text())["assets"]
    for entry in owned:
        data = (HERE / "assets/png" / entry["name"]).read_bytes()
        if sha(data) != entry["sha256"]:
            raise ValueError("Authored resource changed: " + entry["name"])
        files["images/" + entry["name"]] = data
    for entry in json.loads((HERE / "recolor-plan.json").read_text())["assets"]:
        name = entry["name"]
        if Path(name).name != name:
            raise ValueError("Invalid resource name")
        files["images/" + name] = recolor((stock_images / name).read_bytes(), entry)
    return files


def build(native, stock_images, baseline="clean"):
    files = {"gui/ec-eeb001-gui": apply(native), **build_images(stock_images)}
    pins = {}
    for line in (HERE / "baseline-pins.txt").read_text().splitlines():
        old, _new, name = line.split()
        pins[name] = old
    if set(files) != set(pins):
        raise ValueError("Resource set differs from the qualified allow-list")
    if baseline in ("r4", "reflash-r4"):
        upgrade = json.loads((HERE / "upgrade-pins.json").read_text())
        pins = {name: upgrade[name] for name in files}
        if baseline == "reflash-r4":
            pins["gui/ec-eeb001-gui"] = json.loads(
                (HERE / "patch-plan.json").read_text())["base_sha256"]
    elif baseline != "clean":
        raise ValueError("Unknown baseline")
    files["manage.sh"] = (HERE / "manage.sh").read_bytes()
    files["thermal.awk"] = (HERE / "thermal.awk").read_bytes()
    files["pins.sh"] = ("GUI_SHA=" + pins["gui/ec-eeb001-gui"] + "\n"
                        "INIT_SHA=e7537298955e23b7a03cab4c8d7bd241c1dfcc070883cdf51c4889cd255d1bc4\n").encode()
    files["file-pins.txt"] = "".join(
        f"{pins[name]} {sha(data)} {name}\n" for name, data in sorted(files.items())
        if name in pins).encode()
    files["LICENSE-LUCIDE.txt"] = (HERE / "LICENSE-LUCIDE.txt").read_bytes()
    files["validation.json"] = (json.dumps(dict(
        schema=1, baseline=baseline, gui_sha256=sha(files["gui/ec-eeb001-gui"]),
        requires_full_printer_reboot=True, private_vendor_derived_output=True,
        physical_cumulative_acceptance=False), indent=2) + "\n").encode()
    files["CONTROL-SHA256SUMS"] = "".join(
        f"{sha(files[name])}  {name}\n" for name in
        ("manage.sh", "pins.sh", "file-pins.txt", "thermal.awk")).encode()
    files["SHA256SUMS"] = "".join(f"{sha(data)}  {name}\n"
                                  for name, data in sorted(files.items())).encode()
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w") as archive:
        for name, data in sorted(files.items()):
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = 0o755 if name.endswith(".sh") or name.startswith("gui/") else 0o644
            archive.addfile(info, io.BytesIO(data))
    return gzip.compress(buf.getvalue(), mtime=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--stock-images", required=True, type=Path)
    parser.add_argument("--baseline", choices=("clean", "r4", "reflash-r4"), default="clean")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Output already exists")
    data = build(args.native.read_bytes(), args.stock_images, args.baseline)
    with args.output.open("xb") as file:
        file.write(data)
    print("PRIVATE COMPONENT:", args.output, "SHA256:", sha(data))
    print("Disk-only installation; full idle-printer reboot required. Do not publish vendor-derived output.")


if __name__ == "__main__":
    main()
