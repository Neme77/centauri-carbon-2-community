"""Bounded ARM ELF32 RX expansion, shared by the prototype and inert loader test."""
import struct


def expand_rx(raw, payload):
    assert raw[:7] == b'\x7fELF\x01\x01\x01'
    assert struct.unpack_from('<HH', raw, 16) == (2, 40)
    phoff, shoff = struct.unpack_from('<II', raw, 28)
    phsize, phnum, shsize, shnum = struct.unpack_from('<HHHH', raw, 42)
    assert phsize == 32 and shsize == 40 and 0 < phnum < 32 and 0 < shnum < 100
    assert phoff+phnum*phsize <= len(raw) and shoff+shnum*shsize <= len(raw)
    ph = [struct.unpack_from('<8I', raw, phoff+i*32) for i in range(phnum)]
    loads = [(i,p) for i,p in enumerate(ph) if p[0] == 1]
    assert len(loads) == 2
    (ri, rx), (_, rw) = loads
    assert rx[1] == 0 and rx[6] == 5 and rw[6] == 6 and rx[4] == rx[5]
    insert = rx[1]+rx[4]
    assert insert == rw[1] and insert <= shoff
    amount = 65536
    assert rx[7] == rw[7] == amount
    assert all((p[1]-p[2]) % p[7] == 0 for _,p in loads)
    entry = struct.unpack_from('<I', raw, 24)[0]
    assert rx[2] <= entry < rx[2]+rx[4]
    assert 0 < len(payload) < 0xf000
    assert rx[2]+rx[4]+len(payload) < (rw[2] & ~4095)
    assert rw[1]+rw[4] <= len(raw)
    result = bytearray(raw)
    result[insert:insert] = payload+b'\0'*(amount-len(payload))
    struct.pack_into('<I', result, 32, shoff+amount)
    for i,p in enumerate(ph):
        off = phoff+i*32
        if p[1] >= insert and p[1]:
            struct.pack_into('<I', result, off+4, p[1]+amount)
        if i == ri:
            struct.pack_into('<II', result, off+16, p[4]+len(payload), p[5]+len(payload))
    for i in range(shnum):
        off = shoff+amount+i*40
        old_offset = struct.unpack_from('<I', result, off+16)[0]
        if old_offset >= insert:
            struct.pack_into('<I', result, off+16, old_offset+amount)
    return bytes(result)


def reserve_bss(raw, size=32, alignment=16):
    """Reserve zero-initialised extension state after the last native BSS byte."""
    assert raw[:7] == b'\x7fELF\x01\x01\x01'
    assert 0<size<=4096 and alignment>0 and alignment & (alignment-1)==0
    phoff,shoff=struct.unpack_from('<II',raw,28)
    phsize,phnum,shsize,shnum=struct.unpack_from('<HHHH',raw,42)
    assert phsize==32 and shsize==40
    writable=[]
    for i in range(phnum):
        p=struct.unpack_from('<8I',raw,phoff+i*32)
        if p[0]==1 and p[6]==6:writable.append((i,p))
    assert len(writable)==1
    i,p=writable[0];old_end=p[2]+p[5]
    address=(old_end+alignment-1)&~(alignment-1)
    extra=address+size-old_end
    assert address+size<0xf0000000
    sections=[]
    for j in range(shnum):
        s=struct.unpack_from('<10I',raw,shoff+j*40)
        if s[1]==8 and s[2]&2 and s[3]+s[5]==old_end:sections.append((j,s))
    assert len(sections)==1
    j,s=sections[0];result=bytearray(raw)
    struct.pack_into('<I',result,phoff+i*32+20,p[5]+extra)
    struct.pack_into('<I',result,shoff+j*40+20,s[5]+extra)
    return bytes(result),address
