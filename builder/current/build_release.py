#!/usr/bin/env python3
"""Build the community hardware-test candidate from current pinned sources."""
import argparse
from pathlib import Path
import subprocess
HERE=Path(__file__).resolve().parent
REPO=HERE.parents[1]
def run(args,cwd=None):subprocess.run([str(a) for a in args],cwd=cwd,check=True)
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode',choices=('stock','community'),default='stock')
    args=parser.parse_args()
    run(['python3','-m','unittest','discover','-s','builder/current/tests','-p','test_*.py'],cwd=REPO)
    run(['python3',HERE/'launch_helpers/tests/test_launcher.py'])
    run(['python3',HERE/'prepare.py'])
    run(['python3',HERE/'prepare_reactor.py'])
    run(['python3',HERE/'launch_helpers/run_build.py','--root',HERE,'--mode',args.mode,'--check-key'])
    run(['python3',HERE/'launch_helpers/run_build.py','--root',HERE,'--mode',args.mode,'--preflight'])
    run(['python3',HERE/'launch_helpers/run_build.py','--root',HERE,'--mode',args.mode])
    print('CANDIDATE BUILD PASS: validate first boot, Canvas, LAN/WAN, webcam and printing before publishing.')
if __name__=='__main__':main()
