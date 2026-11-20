#!/usr/bin/env python3
"""The backend's header dependencies: a changed header rebuilds the program, and a missing or stale dependency file repairs itself.

The Makefile is copied next to a stand-in compiler, so no real build runs: the stand-in writes the output file for a
compile and a rule for the header list, and can be told to fail while it writes that list. Each check counts the compiles.
"""
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NOW = int(time.time())  # file times stay in the past, so a build made now is newer than all of them
SOURCES = ["main", "mqtt", "console", "control", "panda", "uds", "network_status"]
HEADERS = ["src/spoolman.h", "src/nozzle_expansion.h"]

FAKE_CC = """#!/bin/sh
# Stand-in for gcc: argument -MM writes the dependency rule, anything else is a compile that writes the -o file.
cd "$FAKE_DIR" || exit 2
dep=0; out=""; targets=""; prev=""
for a in "$@"; do
  [ "$a" = "-MM" ] && dep=1
  [ "$prev" = "-o" ] && out=$a
  [ "$prev" = "-MT" ] && targets="$targets $a"
  prev=$a
done
if [ $dep = 1 ]; then
  echo dep >> calls.log
  [ -e FAIL_DEP ] && { echo "forced dependency failure" >&2; exit 1; }
  echo "${targets# }: $(cat headers.txt)"
  for h in $(cat headers.txt); do echo "$h:"; done
else
  echo compile >> calls.log
  # Like the real linker, the stand-in does not create the output directory.
  [ -d "$(dirname "$out")" ] || { echo "cannot open output file $out: No such file or directory" >&2; exit 1; }
  echo built > "$out"
fi
"""


def calls(work):
    log = work / "calls.log"
    return log.read_text().split() if log.exists() else []


def count(work, kind):
    return calls(work).count(kind)


def make(work, *goals, jobs=None, ok=True):
    goals = goals or ("build/cc2-control",)
    cmd = ["make", "-C", str(work), "CROSS=", f"CC={work / 'fakecc.sh'}", *goals]
    if jobs:
        cmd.insert(1, f"-j{jobs}")
    env = dict(os.environ, FAKE_DIR=str(work))
    result = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if (result.returncode == 0) != ok:
        raise AssertionError(f"{' '.join(goals)}: exit {result.returncode}, expected {'success' if ok else 'failure'}\n{result.stdout}{result.stderr}")
    return result


def reset_calls(work):
    (work / "calls.log").write_text("")


def touch(path, seconds):
    os.utime(path, (seconds, seconds))


def fresh(base):
    work = Path(tempfile.mkdtemp(prefix="deps-", dir=base))
    shutil.copy(ROOT / "Makefile", work / "Makefile")
    shutil.copy(ROOT / "VERSION", work / "VERSION")
    shutil.copy(ROOT / "FIRMWARE_VERSION", work / "FIRMWARE_VERSION")
    (work / "src").mkdir()
    for name in SOURCES:
        (work / "src" / f"{name}.c").write_text("/* stand-in */\n")
    for header in HEADERS:
        (work / header).write_text("/* stand-in */\n")
    (work / "headers.txt").write_text(" ".join(HEADERS) + "\n")
    cc = work / "fakecc.sh"
    cc.write_text(FAKE_CC)
    cc.chmod(cc.stat().st_mode | stat.S_IEXEC)
    reset_calls(work)
    old = NOW - 10_000
    for path in [work / "Makefile", work / "VERSION", work / "FIRMWARE_VERSION", *work.glob("src/*")]:
        touch(path, old)
    return work


def bump(work, header, _unused=None):
    """A header changes after the last build: the build products age, the header gets the newer time."""
    deps = work / "build/cc2-control.d"
    binary = work / "build/cc2-control"
    if deps.exists():
        touch(deps, NOW - 5_001)
    if binary.exists():
        touch(binary, NOW - 5_000)
    touch(work / header, NOW - 100)


def main():
    with tempfile.TemporaryDirectory() as base:
        base = Path(base)
        later = NOW - 1_000

        # 1. A first build compiles once and writes the dependency file; an unchanged second build does no work.
        work = fresh(base)
        make(work)
        assert count(work, "compile") == 1, calls(work)
        assert (work / "build/cc2-control.d").is_file(), "no dependency file after a build"
        reset_calls(work)
        make(work)
        assert calls(work) == [], f"an unchanged build did work: {calls(work)}"
        print("PASS: first build writes build/cc2-control.d, an unchanged build does nothing")

        # 2. Each included header rebuilds the program.
        for header in HEADERS:
            reset_calls(work)
            later += 10
            bump(work, header, later)
            make(work)
            assert count(work, "compile") == 1, f"{header} did not rebuild: {calls(work)}"
        print("PASS: a changed header rebuilds the program")

        # 3. Upgrade from the previous Makefile: a binary that is newer than every source, no .d, then a header changes.
        work = fresh(base)
        (work / "build").mkdir()
        binary = work / "build/cc2-control"
        binary.write_text("built by the previous Makefile\n")
        touch(binary, NOW - 5_000)
        later += 10
        bump(work, HEADERS[0], later)
        make(work)
        assert count(work, "compile") == 1, f"upgrade case did not rebuild: {calls(work)}"
        assert (work / "build/cc2-control.d").is_file(), "upgrade case wrote no dependency file"
        reset_calls(work)
        make(work)
        assert calls(work) == [], f"unchanged build after the upgrade did work: {calls(work)}"
        later += 10
        bump(work, HEADERS[1], later)
        reset_calls(work)
        make(work)
        assert count(work, "compile") == 1, f"header change after the upgrade did not rebuild: {calls(work)}"
        print("PASS: an existing binary without a dependency file rebuilds and recovers without make clean")

        # 4. The same upgrade without any header change: the stale binary is not accepted blindly either.
        work = fresh(base)
        (work / "build").mkdir()
        binary = work / "build/cc2-control"
        binary.write_text("built by the previous Makefile\n")
        touch(binary, NOW - 5_000)
        make(work)
        assert (work / "build/cc2-control.d").is_file(), "no dependency file after the first build of an upgrade"
        assert count(work, "compile") == 1, f"binary of the previous Makefile was kept: {calls(work)}"
        print("PASS: the first build after an upgrade replaces a binary that has no dependency file")

        # 5. The dependency file is deleted after a good build.
        work = fresh(base)
        make(work)
        (work / "build/cc2-control.d").unlink()
        reset_calls(work)
        make(work)
        assert (work / "build/cc2-control.d").is_file(), "deleted dependency file was not recreated"
        reset_calls(work)
        later += 10
        bump(work, HEADERS[0], later)
        make(work)
        assert count(work, "compile") == 1, f"header change after the recovery did not rebuild: {calls(work)}"
        print("PASS: a deleted dependency file is recreated and the next header change rebuilds")

        # 6. Writing the header list fails (after an earlier good build); the failure is never accepted as up to date.
        work = fresh(base)
        make(work)
        later += 10
        bump(work, HEADERS[0], later)
        (work / "FAIL_DEP").write_text("")
        reset_calls(work)
        make(work, ok=False)
        assert not (work / "build/cc2-control.d.tmp").exists(), "a half-written dependency file was left behind"
        make(work, ok=False)  # still failing: no silent acceptance on the retry
        (work / "FAIL_DEP").unlink()
        reset_calls(work)
        make(work)
        assert count(work, "compile") == 1, f"build after the failure did not repair the state: {calls(work)}"
        assert (work / "build/cc2-control.d").is_file(), "dependency file not repaired"
        reset_calls(work)
        make(work)
        assert calls(work) == [], f"unchanged build after the repair did work: {calls(work)}"
        later += 10
        bump(work, HEADERS[1], later)
        reset_calls(work)
        make(work)
        assert count(work, "compile") == 1, f"header-only change after the repair did not rebuild: {calls(work)}"
        print("PASS: a failure while writing the header list is retried, repaired, and a later header change rebuilds")

        # 7. The first build fails while writing the header list: no binary may be accepted afterwards.
        work = fresh(base)
        (work / "FAIL_DEP").write_text("")
        make(work, ok=False)
        (work / "FAIL_DEP").unlink()
        make(work)
        assert (work / "build/cc2-control").is_file() and (work / "build/cc2-control.d").is_file()
        reset_calls(work)
        make(work)
        assert calls(work) == [], f"unchanged build did work: {calls(work)}"
        print("PASS: a failed first dependency run leaves nothing that a later build trusts")

        # 8. One parallel make builds once.
        work = fresh(base)
        make(work, jobs=4)
        assert count(work, "compile") == 1, f"make -j4 compiled {count(work, 'compile')} times: {calls(work)}"
        reset_calls(work)
        make(work, jobs=4)
        assert calls(work) == [], f"unchanged make -j4 did work: {calls(work)}"
        print("PASS: make -j4 builds once and an unchanged one does nothing")

        # 9. make clean removes the build directory and does not run the compiler.
        work = fresh(base)
        make(work)
        reset_calls(work)
        make(work, "clean")
        assert not (work / "build").exists(), "make clean left build/"
        assert calls(work) == [], f"make clean ran the compiler: {calls(work)}"
        print("PASS: make clean runs no compiler")


        # 10. `make clean test` is one invocation in the CI: the build directory must exist again for the build after clean.
        work = fresh(base)
        make(work)
        reset_calls(work)
        make(work, "clean", "build/cc2-control")
        assert (work / "build/cc2-control").is_file() and (work / "build/cc2-control.d").is_file(), "clean and build in one run left no build"
        assert count(work, "compile") == 1, calls(work)
        reset_calls(work)
        make(work)
        assert calls(work) == [], f"unchanged build after clean and build did work: {calls(work)}"
        print("PASS: make clean and a build in one invocation builds again")

if __name__ == "__main__":
    sys.exit(main())
