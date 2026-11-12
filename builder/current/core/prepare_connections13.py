"""Prepare the native Connections shim sources for the exact local .13 trial."""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from connections_component import read_runtime, check_native
from stage_stock13 import PRINTER_PULLBACK13_SHA256


CAMERA13 = '90da7634c21b74b3bf943d68129da78668b47f9706620e158283697ab95553a7'
CAMERA07 = 'ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99'
PRINTER07 = '19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745'
HOOKS = {
    'camera': (
        (0x17ae44, 0x1831cc, 0xe92d4800, 0xe28db004),
        (0x17af78, 0x183300, 0xe92d4870, 0xe28db010),
    ),
    'printer': (
        (0x8340d0, 0x83b860, 0xe92d4810, 0xe28db008),
        (0x833f04, 0x83b694, 0xe92d4810, 0xe28db008),
        (0x87dec0, 0x885650, 0xe92d4810, 0xe28db008),
    ),
}


def executable_words(data, address):
    phoff, = struct.unpack_from('<I', data, 28)
    entry_size, count = struct.unpack_from('<HH', data, 42)
    for index in range(count):
        kind, offset, va, _, size, _, flags, _ = struct.unpack_from(
            '<8I', data, phoff + index * entry_size)
        if kind == 1 and flags & 1 and va <= address and address + 8 <= va + size:
            return struct.unpack_from('<II', data, offset + address - va)
    raise RuntimeError(f'Connections hook outside executable segment: {address:#x}')


def replace_once(data, before, after):
    if data.count(before) != 1:
        raise RuntimeError('Unexpected Connections source anchor: ' + before)
    return data.replace(before, after)


def prepare(archive, camera, printer, destination):
    for path, digest in ((camera, CAMERA13), (printer, PRINTER_PULLBACK13_SHA256)):
        check_native(path.parent, {path.name: digest})
    archive_files = read_runtime(archive, lan_preview=True)
    prefix = 'source/compatibility/source/'
    files = {name: archive_files[prefix + name] for name in
             ('network_compat.c', 'net_core.c', 'net_core.h',
              'sha256_file.c', 'sha256_file.h')}
    source = files['network_compat.c'].decode('utf-8')
    for role, path in (('camera', camera), ('printer', printer)):
        data = path.read_bytes()
        for old, new, first, second in HOOKS[role]:
            if executable_words(data, new) != (first, second):
                raise RuntimeError(f'Changed .13 {role} hook at {new:#x}')
            before = f'{{{old:#x},{{{first:#x},{second:#x}}},NULL,NULL}}'
            after = f'{{{new:#x},{{{first:#x},{second:#x}}},NULL,NULL}}'
            source = replace_once(source, before, after)
    source = replace_once(source, CAMERA07, CAMERA13)
    source = replace_once(source, PRINTER07, PRINTER_PULLBACK13_SHA256)
    source = replace_once(source, '#include <arpa/inet.h>', '#include <arpa/inet.h>\n#include <errno.h>')
    source = replace_once(source, 'static void refuse(const char *reason) {', '''static int write_all(int fd, const char *data, size_t size) {
    while (size) {
        ssize_t written = write(fd, data, size);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return 0;
        data += written;
        size -= (size_t)written;
    }
    return 1;
}

static void refuse(const char *reason) {''')
    for args in ('2,prefix,sizeof(prefix)-1', '2,reason,strlen(reason)', '2,"\\n",1'):
        source = replace_once(source, f'(void)write({args})', f'(void)write_all({args})')
    source = replace_once(
        source, 'if(n>0&&(size_t)n<sizeof(line))(void)write(fd,line,(size_t)n);close(fd);',
        'int complete=n>0&&(size_t)n<sizeof(line)&&write_all(fd,line,(size_t)n);'
        'if(close(fd)!=0||!complete)unlink(path);')
    files['network_compat.c'] = source.encode()
    destination.mkdir()
    for name, data in files.items():
        (destination / name).write_bytes(data)
    manifest = {
        'camera_sha256': CAMERA13,
        'printer_sha256': PRINTER_PULLBACK13_SHA256,
        'sources': {name: hashlib.sha256(data).hexdigest() for name, data in files.items()},
    }
    (destination / 'source-manifest.json').write_bytes(
        (json.dumps(manifest, indent=2) + '\n').encode())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('archive', 'camera', 'printer', 'destination'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    prepare(args.archive, args.camera, args.printer, args.destination)
    print('PASS: five .13 Connections hooks verified and native shim sources prepared')
