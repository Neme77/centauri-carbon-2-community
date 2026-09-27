#!/usr/bin/env bash
# Usage: bash install_squashfs_461.sh NEW_DESTINATION
# Needs git, make, C compiler, Python 3, zlib and liblzma development files.
set -euo pipefail
if [ "$#" -ne 1 ]; then echo 'Usage: install_squashfs_461.sh NEW_DESTINATION' >&2; exit 2; fi
if [ -e "$1" ] || [ -L "$1" ]; then echo 'Destination already exists; choose a new directory' >&2; exit 1; fi
mkdir -p -- "$1"
cc2_dest=$(cd -- "$1" && pwd)
cc2_commit=d8cb82d9840330f9344ec37b992595b5d7b44184
# No global Git configuration, system installation, or removal of existing trees.
git clone https://github.com/plougher/squashfs-tools.git "$cc2_dest/source"
git -C "$cc2_dest/source" checkout --detach "$cc2_commit"
test "$(git -C "$cc2_dest/source" rev-parse HEAD)" = "$cc2_commit"
# XZ is required by CC2; other optional compressors are disabled explicitly.
make -C "$cc2_dest/source/squashfs-tools" -j2 XZ_SUPPORT=1 GZIP_SUPPORT=1 LZO_SUPPORT=0 LZ4_SUPPORT=0 ZSTD_SUPPORT=0
mkdir "$cc2_dest/bin"
cp "$cc2_dest/source/squashfs-tools/mksquashfs" "$cc2_dest/source/squashfs-tools/unsquashfs" "$cc2_dest/bin/"
python3 - "$cc2_dest" "$cc2_commit" <<'PY'
import hashlib,json,subprocess,sys
from pathlib import Path
p=Path(sys.argv[1])
def output(args):
 r=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 return r.stdout
record={'source_commit':sys.argv[2],'profile':'XZ+gzip; LZO/LZ4/ZSTD disabled','sha256':{n:hashlib.sha256((p/'bin'/n).read_bytes()).hexdigest() for n in ['mksquashfs','unsquashfs']},'compiler':output(['cc','--version']),'versions':{n:output([str(p/'bin'/n),'-version']) for n in ['mksquashfs','unsquashfs']}}
(p/'toolchain-manifest.json').write_text(json.dumps(record,indent=2)+'\n')
print('Created',p/'toolchain-manifest.json')
PY
