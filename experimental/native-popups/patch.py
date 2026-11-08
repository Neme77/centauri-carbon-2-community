# SPDX-License-Identifier: GPL-3.0-or-later
"""Add the long-press pop-ups to an owner-supplied CC2 Community screen program.

Never loads or executes the vendor program. Only the exact supported input is
accepted. The linked payload (one PT_LOAD) is appended at the end of the file and
mapped by turning the PT_NOTE program header (the ABI tag, unused at run time)
into a PT_LOAD above all existing segments. The Home view's show entry in the
program's view table is pointed at the payload's entry, which calls the
original function first.
"""
import hashlib
import struct

# CC2 Community V4.4.3 screen program (ELEGOO 02.01.01.07 with the native display refresh).
V443 = {
    'input_sha256': 'db09fa37d378df259427aaac392a341282e08ceb26cd87603542c0dcbcbb91d1',
    'home_show': 0x0005c1dc,    # ui_view_home_home_show
    'view_entry': 0x0018fe14,   # its pointer in the view table (.rodata)
}
PT_LOAD, PT_NOTE = 1, 4
PF_RWX = 7
ALIGN = 0x10000


def program_headers(elf):
    phoff, = struct.unpack_from('<I', elf, 0x1c)
    phentsize, phnum = struct.unpack_from('<HH', elf, 0x2a)
    return [(phoff + i * phentsize, struct.unpack_from('<8I', elf, phoff + i * phentsize)) for i in range(phnum)]


def file_offset(elf, va):
    for _, (kind, off, vaddr, _, filesz, _, _, _) in program_headers(elf):
        if kind == PT_LOAD and vaddr <= va and va + 4 <= vaddr + filesz:
            return off + va - vaddr
    raise ValueError(f'address {va:#x} is not file-backed')


def payload_segment(elf):
    """(vaddr, bytes, memsz, entry) of a linked payload with exactly one PT_LOAD."""
    if elf[:4] != b'\x7fELF' or elf[4] != 1 or elf[5] != 1:
        raise ValueError('payload is not a 32-bit little-endian ELF')
    entry, = struct.unpack_from('<I', elf, 0x18)
    loads = [h for _, h in program_headers(elf) if h[0] == PT_LOAD and h[4]]
    if len(loads) != 1:
        raise ValueError('payload must have exactly one loaded segment')
    _, off, vaddr, _, filesz, memsz, _, _ = loads[0]
    if not vaddr <= entry < vaddr + filesz:
        raise ValueError('payload entry is outside its segment')
    return vaddr, bytes(elf[off:off + filesz]), memsz, entry


def apply(program, payload, profile=V443):
    """The patched program, or ValueError. Neither input is modified."""
    elf = bytearray(program)
    if hashlib.sha256(elf).hexdigest() != profile['input_sha256']:
        raise ValueError('unsupported screen program (sha256 differs)')
    vaddr, code, memsz, entry = payload_segment(payload)
    if vaddr % ALIGN or memsz < len(code):
        raise ValueError('payload segment must be 64 KiB aligned')
    headers = program_headers(elf)
    top = max(v + m for _, (k, _, v, _, _, m, _, _) in headers if k == PT_LOAD)
    if vaddr < top:
        raise ValueError(f'payload at {vaddr:#x} overlaps the program (ends {top:#x})')
    notes = [i for i, (_, h) in enumerate(headers) if h[0] == PT_NOTE]
    if len(notes) != 1:
        raise ValueError('expected exactly one PT_NOTE program header')
    if any(h[0] == PT_LOAD for _, h in headers[notes[0] + 1:]):
        raise ValueError('a PT_LOAD follows PT_NOTE; loadable segments would leave address order')
    view = file_offset(elf, profile['view_entry'])
    if struct.unpack_from('<I', elf, view)[0] != profile['home_show']:
        raise ValueError('the view table entry is not the Home show function')

    offset = len(elf) + (vaddr - len(elf)) % ALIGN      # file offset congruent with the address
    elf += bytes(offset - len(elf)) + code
    struct.pack_into('<8I', elf, headers[notes[0]][0], PT_LOAD, offset, vaddr, vaddr, len(code), memsz, PF_RWX, ALIGN)
    struct.pack_into('<I', elf, view, entry)
    return bytes(elf)


if __name__ == '__main__':
    import sys
    if len(sys.argv) != 4:
        raise SystemExit('usage: patch.py SCREEN_PROGRAM PAYLOAD_ELF OUTPUT')
    out = apply(open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read())
    with open(sys.argv[3], 'wb') as f:
        f.write(out)
    print('output sha256', hashlib.sha256(out).hexdigest())
