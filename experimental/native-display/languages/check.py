"""Public synthetic UI/preference tests; optional exact-image/ARM checks."""
import argparse
import json
import re
import subprocess
import tempfile
from pathlib import Path
from literal_catalog import LABELS, load_catalogs

HERE = Path(__file__).resolve().parent

def run(args):
    subprocess.run([str(x) for x in args], check=True, timeout=180)

def catalogs():
    keys = None
    formats = re.compile(r'%(?:\d+\$)?[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diouxXfFeEgGaAcspn%]')
    def pairs(items):
        result = {}
        for key, value in items:
            assert key not in result and isinstance(value, str) and value.strip() and '\0' not in value
            assert formats.findall(key) == formats.findall(value), key
            result[key] = value
        return result
    for lang in ('hu', 'cs', 'pl'):
        data = json.loads((HERE/'locales'/f'{lang}.json').read_text(encoding='utf-8-sig'), object_pairs_hook=pairs)
        assert keys is None or set(data) == keys
        keys = set(data)
        print(lang, len(data), 'catalog entries checked', flush=True)
    for data in load_catalogs(HERE/'locales'):
        for key in LABELS:
            assert key in data and data[key] != key, key

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--arm-cc')
    parser.add_argument('--catalogs-only', action='store_true')
    args = parser.parse_args()
    catalogs()
    if args.catalogs_only:
        return
    with tempfile.TemporaryDirectory(prefix='display-language-', dir='/var/tmp') as tmp:
        work = Path(tmp)
        # No extracted vendor strings or addresses are needed for callback fixtures.
        names = ','.join(json.dumps('Language '+str(i)) for i in range(14))
        header = '#define ND_STRINGS 648\n'
        header += 'static const char *const nd_language_names[14]={' + names + '};\n'
        header += 'static const char *const nd_new_text[3][648]={' + ','.join('{' + ','.join(['"Fixture"']*648) + '}' for _ in range(3)) + '};\n'
        (work/'native_language_data.h').write_text(header)
        # Synthetic addresses exercise pointer identity without vendor literals.
        (work/'native_literal_data.h').write_text(
            '#include <stdint.h>\n'
            'static const char fixture_literal[]="Idle";\n'
            'struct nd_literal { uintptr_t address; const char *value[3]; };\n'
            'static const struct nd_literal nd_literals[]={'
            '{(uintptr_t)fixture_literal,{"HU literal","CS literal","PL literal"}}};\n'
            '#define ND_LITERAL_COUNT 1\n')
        flags = ['-O2', '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror']
        targets = [('host', 'gcc', [], [])]
        if args.arm_cc:
            targets.append(('ARM', args.arm_cc, ['-static', '-mcpu=cortex-a7', '-mfpu=neon-vfpv4', '-mfloat-abi=hard'], ['qemu-arm']))
        for name, cc, extra, runner in targets:
            for test in ('test_language_core.c', 'test_display_language.c'):
                exe = work/(name+'-'+test)
                run([cc, *flags, *extra, '-I'+str(work), HERE/test, HERE/'language_core.c', '-o', exe])
                run([*runner, exe])
        print('PASS language callbacks, native themed Back and persistence; no printer accessed')

if __name__ == '__main__':
    main()
