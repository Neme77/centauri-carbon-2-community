"""Compile the real HTTP digest gate; keep published managers and reject others."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/connections_http.h').read_text()
begin = source.index('static int connections_helper_digest(')
end = source.index('\nstatic int connections_program(', begin)
gate = source[begin:end]
pins = (ROOT / 'src/connections/pin.h').read_text()
# Independent released identities: do not derive expected acceptance from the pins.
accepted = [
    '2a4dd2dd8e23aa3446348973ba6b8fa90a1fda16fcd96626015f8f0b816e9d62',
    '3f9e8ca97fcb668c694bc93aa08dc1753ddf50d1cddc17567acb274c91dbaaf4',
    'c0e34d034c48d1bb3b81f5bb67b1b051292ebc27a72f0009802933788447df87',
    '136ac6f5081b733596cbf9e1518920e1e6a64612d35f2e3547cda9ec36289a65',
]
tests = [f'assert(connections_helper_digest("{value}") == 1);' for value in accepted]
rejected = ['', '0' * 64, accepted[-1].upper(), accepted[-1] + '0', accepted[-1][:-1]]
for value in accepted:
    for i, char in enumerate(value):
        rejected.append(value[:i] + ('0' if char != '0' else '1') + value[i + 1:])
tests += [f'assert(connections_helper_digest("{value}") == 0);' for value in rejected]
# Verify metadata/fd checks remain in the actual entry point; accepting a known
# digest must not authorize writable, linked, non-executable or foreign files.
program = source[end:source.index('\nstatic void connections_reap(', end)]
for required in ('O_NOFOLLOW', 'S_ISREG(st.st_mode)', 'st.st_uid == geteuid()',
                 '!(st.st_mode & 0022)', '(st.st_mode & 0111)', 'st.st_nlink == 1',
                 'sha256_fd(fd, digest)', 'connections_helper_digest(digest)'):
    assert required in program, required
assert len(re.findall(r'^#define CC2_CONNECTIONS_\w+_SHA ', pins, re.M)) == 4
with tempfile.TemporaryDirectory(prefix='cc2-connections-pins-') as directory:
    directory = Path(directory)
    unit = directory / 'pins.c'
    binary = directory / 'pins'
    unit.write_text('#include <assert.h>\n#include <string.h>\n' + pins + '\n' + gate
                    + '\nint main(void) {\n' + '\n'.join(tests) + '\nreturn 0;\n}\n')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(unit), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print(f'PASS: four released manager digests, {len(rejected)} refusals, unchanged metadata/fd checks')
