"""Bounded translations for native labels that bypass get_text().

Match pinned executable addresses, never arbitrary user text or filenames.
The generated address/translation table stays private with the other ELF inputs.
"""
import json

LABELS = (
    'Ready to create', 'Idle', 'Nozzle', 'Build plate', 'Chamber',
    'Canvas connected', 'System Version', 'Current Version', 'Offline Update',
    'Note: No update detected', 'OFF', 'Load', 'Unload', 'Edit', 'Model Fan',
    'Note: Select a slot first, then click Load/Unload',
)

# Copied instructions contain no PC-relative operations. Refuse any other entry.
LABEL_ENTRIES = {
    'lv_label_set_text': (0x1234d4, bytes.fromhex('70402de918d04de2')),
    'lv_label_set_text_static': (0x1237c0, bytes.fromhex('70402de90040a0e1')),
}

def literal_rows(native, catalogs):
    rows = []
    for key in LABELS:
        needle = key.encode('utf-8') + b'\0'
        addresses = set()
        for segment in native.loads:
            start = segment['p_offset']
            data = native.data[start:start + segment['p_filesz']]
            offset = data.find(needle)
            while offset >= 0:
                if offset == 0 or data[offset-1] == 0:
                    addresses.add(segment['p_vaddr'] + offset)
                offset = data.find(needle, offset + 1)
        if not addresses:
            raise ValueError('Missing qualified display literal: ' + key)
        translations = [catalog[key] for catalog in catalogs]
        if any(not value or value == key for value in translations):
            raise ValueError('Missing literal translation: ' + key)
        rows.extend((address, translations) for address in sorted(addresses))
    return sorted(rows)

def header(rows, cstr):
    result = ['/* Generated private pinned-literal addresses. */', '#include <stdint.h>',
              'struct nd_literal { uintptr_t address; const char *value[3]; };',
              'static const struct nd_literal nd_literals[]={']
    result += ['{0x%xu,{%s}},' % (address, ','.join(map(cstr, values)))
               for address, values in rows]
    result += ['};', '#define ND_LITERAL_COUNT (sizeof(nd_literals)/sizeof(nd_literals[0]))']
    return '\n'.join(result) + '\n'

def load_catalogs(directory):
    return [json.loads((directory/(lang+'.json')).read_text(encoding='utf-8-sig'))
            for lang in ('hu', 'cs', 'pl')]
