"""Run the affected existing C harnesses before packaging the merged firmware."""
import ast
import hashlib
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
import zipfile

PROJECT = Path(__file__).resolve().parents[2]
SOURCE = PROJECT / 'work/cc2-13-port/source'
APP = SOURCE / 'cc2-control'
NAMES = (
    'test_control_actions.c', 'test_screw_history.c', 'test_screw_tram_end.c',
    'test_gcode_layer_path.c', 'test_layer_path_deadline.c',
    'test_usb_print_start.c', 'test_calibrated_start.c', 'test_external_feed_start.c',
    'test_queue_tracking.c', 'test_bed_drying.c', 'test_chamber_preheat.c',
    'test_printer_replies.c', 'test_backup.c',
)

driver = APP / 'tests/test_c_harnesses.py'
module = ast.parse(driver.read_text())
# Use the checked-in flags and source list while selecting the affected tests.
definitions = []
for statement in module.body:
    if isinstance(statement, ast.With):
        break
    definitions.append(statement)
scope = {'__file__': str(driver)}
exec(compile(ast.Module(body=definitions, type_ignores=[]), str(driver), 'exec'), scope)
with TemporaryDirectory(prefix='cc2-444-regression-') as scratch:
    for name in NAMES:
        binary = Path(scratch) / name[:-2]
        command = ['cc', *scope['FLAGS'], str(APP / 'tests' / name),
                   *map(str, scope['HARNESSES'][name]), '-o', str(binary), '-lm']
        subprocess.run(command, check=True, timeout=180)
        subprocess.run([binary], cwd=scratch, check=True, timeout=120)
        print(f'PASS integrated regression {name}', flush=True)
subprocess.run(['python3', '-B', str(SOURCE / 'builder/current/tests/test_cc2_control_source_snapshot.py')],
               check=True, timeout=60)
snapshot = SOURCE / 'builder/current/components/cc2-control/source/source.zip'
with zipfile.ZipFile(snapshot) as archive:
    for name in archive.namelist():
        if not name.endswith('/'):
            if archive.read(name) != (APP / name).read_bytes():
                raise RuntimeError(f'Source snapshot differs: {name}')
print('PASS integrated source SHA256 ' + hashlib.file_digest(snapshot.open('rb'), 'sha256').hexdigest())
