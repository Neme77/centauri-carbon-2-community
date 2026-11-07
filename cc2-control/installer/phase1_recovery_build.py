#!/usr/bin/env python3
"""Build the host-only recovery helper for the released 1.2.26 profile."""
import argparse
from pathlib import Path

from app_binding_build import LIFE, compile_arm, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    output = parser.parse_args().output
    if output.exists() or output.is_symlink():
        parser.error("output already exists")
    defines = {
        "SC_APP_SHA": "85af6b14faccdc2e3dfc2f679339c8ef4db5ce82d2c42a4591cdf10fc8081c96",
        "SC_PROFILE_SHA": "9efc21a14f3da203a0fc8b0cf710602ab4bbf02bef6a3517c827a52b89f67766",
        "SC_FRESH_PATH": "/opt/usr/cc2-rfid/fresh-reader",
        "SC_FRESH_SHA": "3dd6d8699169c800689e5ed2c047a2d3cc43386d3fdad59adc41b929111b045a",
    }
    flags = [
        "-DSC_HOST_RECOVERY_ONLY=1", "-DSC_STOCK_BOOTSTRAP=1",
        "-DSC_WITH_CAVITY=1", "-DSC_CAVITY_BOOTSTRAP_OPTIONAL=1",
        "-DSC_VERIFY_CAVITY_CHANNEL=1",
    ]
    data = compile_arm(LIFE / "coordinator.c", output, defines, flags)
    print(f"{sha(data)}  {output}")


if __name__ == "__main__":
    main()
