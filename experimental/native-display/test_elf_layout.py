"""ARM/Linux loader check using an entirely synthetic executable and private RW data.

The vendor GUI is never executed. The same expansion function as production is
tested with a native stack frame, appended return tail and relocated data.
"""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from elf_layout import expand_rx, reserve_bss


def fixture():
    raw=bytearray(0x1300)
    ident=b'\x7fELF\x01\x01\x01'+b'\0'*9
    struct.pack_into('<16sHHIIIIIHHHHHH',raw,0,ident,2,40,1,0x10200,52,0x1200,0x5000400,52,32,3,40,5,4)
    rows=[(1,0,0x10000,0x10000,0x1000,0x1000,5,65536),
          (1,0x1000,0x21000,0x21000,4,8,6,65536),
          (0x6474e551,0,0,0,0,0,6,16)]
    for i,p in enumerate(rows):struct.pack_into('<8I',raw,52+i*32,*p)
    # _start: call inert Home fixture, exit with its integer return value.
    struct.pack_into('<III',raw,0x200,0xeb00000e,0xe3a07001,0xef000000)
    # Home: original nine-register stack frame, load test value from RW, return.
    struct.pack_into('<5I',raw,0x240,0xe92d4ff0,0xe3014000,0xe3404002,0xe5940000,0xe8bd8ff0)
    struct.pack_into('<I',raw,0x1000,7)
    names=b'\0.text\0.data\0.bss\0.shstrtab\0';raw[0x1100:0x1100+len(names)]=names
    sections=[(0,)*10,(1,1,6,0x10200,0x200,0x54,0,0,4,0),
              (7,1,3,0x21000,0x1000,4,0,0,4,0),(13,8,3,0x21004,0x1004,4,0,0,4,0),(18,3,0,0,0x1100,len(names),0,0,1,0)]
    for i,s in enumerate(sections):struct.pack_into('<10I',raw,0x1200+i*40,*s)
    return bytes(raw)


def main():
    assert shutil.which('qemu-arm'), 'Existing local ARM emulator required'
    original=fixture();patched=bytearray(original)
    delta=(0x11000-0x10250-8)//4
    struct.pack_into('<I',patched,0x250,0xea000000|(delta&0xffffff))
    tail=struct.pack('<7I',0xe3011010,0xe3401002,0xe5912000,0xe2800005,0xe0800002,0xe5810000,0xe8bd8ff0)
    changed,state=reserve_bss(expand_rx(bytes(patched),tail))
    assert state==0x21010
    with tempfile.TemporaryDirectory(prefix='cc2-inert-elf-') as directory:
        for name,data,wanted in [('original',original,7),('expanded',changed,12)]:
            p=Path(directory)/name;p.write_bytes(data);p.chmod(0o700)
            r=subprocess.run(['qemu-arm',str(p)],capture_output=True,timeout=5)
            assert r.returncode==wanted,(name,r.returncode,r.stdout,r.stderr)
    invalid=[]
    b=bytearray(original);b[0]=0;invalid.append((bytes(b),b'1234'))
    invalid.append((original,b'x'*65536))
    b=bytearray(original);struct.pack_into('<I',b,52+32+8,0x11000);invalid.append((bytes(b),b'1234'))
    b=bytearray(original);struct.pack_into('<I',b,52+32+8,0x21001);invalid.append((bytes(b),b'1234'))
    for data,tail in invalid:
        try:expand_rx(data,tail)
        except AssertionError:pass
        else:raise AssertionError('Malformed ELF or overflowing tail accepted')
    print('PASS: actual ARM/Linux ELF loading before/after RX expansion; original RW value, new zero-initialised writable BSS, native stack return and appended code; malformed/alignment/overlap/size refusal. Only synthetic programs ran.')


if __name__=='__main__':main()
