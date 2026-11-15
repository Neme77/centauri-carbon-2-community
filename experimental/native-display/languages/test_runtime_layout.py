"""Structural checks of the actual patched ELF metadata, without executing it."""
import io
import struct
import unittest
from native_model import Native
from prepare_runtime import inject_dependency, LIBRARY
from elftools.elf.elffile import ELFFile

class Layout(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.native=Native()
        cls.output=inject_dependency(cls.native)
        cls.after=ELFFile(io.BytesIO(cls.output))

    def test_dependency_added_once(self):
        def names(elf):
            segment=next(s for s in elf.iter_segments() if s['p_type']=='PT_DYNAMIC')
            return [t.needed for t in segment.iter_tags('DT_NEEDED')]
        self.assertEqual(names(self.after),names(self.native.elf)+[LIBRARY])

    def test_original_loads_unchanged(self):
        old=self.native.loads
        new=[s for s in self.after.iter_segments() if s['p_type']=='PT_LOAD']
        self.assertEqual(len(new),len(old)+1)
        self.assertEqual([dict(s.header) for s in old],[dict(s.header) for s in new[:2]])
        self.assertEqual(new[-1]['p_flags'],6)
        self.assertEqual(new[-1]['p_vaddr'],0x1400000)
        self.assertEqual(new[-1]['p_offset']%65536,0)
        for first,second in zip(new,new[1:]):
            self.assertLessEqual(first['p_vaddr']+first['p_memsz'],second['p_vaddr'])

    def test_native_functions_unmodified(self):
        for section in self.native.elf.iter_sections():
            if section['sh_flags']&4:
                start=section['sh_offset'];end=start+section['sh_size']
                self.assertEqual(self.output[start:end],self.native.data[start:end],section.name)

    def test_data_and_tls_unmodified(self):
        load=self.native.loads[1]
        start=load['p_offset'];end=start+load['p_filesz']
        self.assertEqual(self.output[start:end],self.native.data[start:end])
        def tls(elf):return [dict(s.header) for s in elf.iter_segments() if s['p_type']=='PT_TLS']
        self.assertEqual(tls(self.after),tls(self.native.elf))

    def test_existing_symbols_preserved(self):
        def symbols(elf):
            return [(s.name,s['st_value'],s['st_size']) for s in elf.get_section_by_name('.symtab').iter_symbols()]
        self.assertEqual(symbols(self.after),symbols(self.native.elf))

    def test_dynamic_section_matches_program_header(self):
        segment=next(s for s in self.after.iter_segments() if s['p_type']=='PT_DYNAMIC')
        section=self.after.get_section_by_name('.dynamic')
        self.assertEqual(section['sh_addr'],segment['p_vaddr'])
        self.assertEqual(section['sh_offset'],segment['p_offset'])
        self.assertEqual(section['sh_size'],segment['p_filesz'])
        tags=list(segment.iter_tags())
        self.assertEqual(tags[-1].entry.d_tag,'DT_NULL')
        oldseg=next(s for s in self.native.elf.iter_segments() if s['p_type']=='PT_DYNAMIC')
        original=[(t.entry.d_tag,t.entry.d_val) for t in oldseg.iter_tags() if t.entry.d_tag not in ('DT_STRTAB','DT_STRSZ','DT_NULL')]
        updated=[(t.entry.d_tag,t.entry.d_val) for t in tags if t.entry.d_tag not in ('DT_STRTAB','DT_STRSZ','DT_NULL')]
        self.assertEqual(updated[:-1],original)

if __name__=='__main__':unittest.main(verbosity=2)
