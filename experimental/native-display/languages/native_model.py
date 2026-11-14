"""Read-only ELF model for the exact Universal07 HU display input."""
import hashlib
import io
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
import os
# Private inputs may live outside the checkout. Never commit extracted firmware.
INPUT_ROOT = Path(os.environ.get("CC2_DISPLAY_INPUTS", str(HERE/"signed-display-inputs")))
from elftools.elf.elffile import ELFFile

GUI_SHA = '2adb42998c2778d5fed4a2351759c377afc74fb1083a4ed007b22a38af90bc4e'

class Native:
    def __init__(self):
        self.data = (INPUT_ROOT/'opt/bin/ec-eeb001-gui').read_bytes()
        if hashlib.sha256(self.data).hexdigest() != GUI_SHA:
            raise ValueError('Unexpected native GUI input')
        self.elf = ELFFile(io.BytesIO(self.data))
        self.loads = [s for s in self.elf.iter_segments() if s['p_type']=='PT_LOAD']

    def read(self, address, length):
        for s in self.loads:
            if s['p_vaddr'] <= address and address+length <= s['p_vaddr']+s['p_filesz']:
                offset = s['p_offset']+address-s['p_vaddr']
                return self.data[offset:offset+length]
        raise ValueError('Unmapped file-backed address: '+hex(address))

    def word(self, address):
        return struct.unpack('<I', self.read(address,4))[0]

    def string(self, address):
        result = bytearray()
        for offset in range(16384):
            value = self.read(address+offset,1)
            if value == b'\0':
                return result.decode('utf-8')
            result += value
        raise ValueError('Unterminated native string')

    def image(self, index):
        if not 0 <= index < 640:
            raise ValueError('Image index outside native bounds')
        return self.string(self.word(0x17d200+4*index))

    def text(self, language, index):
        if not 0 <= language < 11 or not 0 <= index < 648:
            raise ValueError('Language index outside original bounds')
        return self.string(self.word(0x12ca28+4*(language*648+index)))

    def branch_target(self, address):
        instruction = self.word(address)
        if instruction & 0xff000000 != 0xeb000000:
            raise ValueError('Expected unconditional ARM BL')
        displacement = instruction & 0xffffff
        if displacement & 0x800000:
            displacement -= 0x1000000
        return address+8+4*displacement
