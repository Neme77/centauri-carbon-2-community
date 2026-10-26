"""Qualified, standalone pull-back patch policy. Original two-byte change: Noahwhite2014 #172."""
import hashlib
import re
from decimal import Decimal, InvalidOperation
from pathlib import PurePosixPath
from patch_pullback import transform, ORIGINAL_SHA256, EXTENDED_SHA256

BASE = '/opt/usr/cc2-reactor-runtime-v2'
STATE = '/opt/usr/cc2-canvas-pullback-1000'
HOOK = '/etc/init.d/cc2-reactor-launch'
TARGETS = {'boot': HOOK, 'launcher': BASE+'/launcher.sh', 'sums': BASE+'/SHA256SUMS', 'program': BASE+'/elegoo_printer'}
CONFIGS = {'main': '/opt/inst/printer_dsp.cfg', 'autosave': '/opt/usr/cfg/autosave.cfg', 'autosave_backup': '/opt/usr/cfg/autosave_backup.cfg'}
PINS = {
    'program': ORIGINAL_SHA256,
    'launcher': 'a260865964759898cf47ed8e30e20eb8981506c1a22ada32611b9911b36b47fc',
    'boot': '8d4a19c9be1f6f85fe5ab9d3f116aa84400e7e6a77c455e66da636373fdaac43',
    'preflight.sh': '27c3a5bfad9953ac2c64af39a86fd048c6affdd88141a5ea6d76e64b8bee05b2',
    'libcc2-reactor-v2.so': '4639364c68489ec40c4e0c4f425550f3aa19e44c2ee61ed641affdc560e7e92c',
    'cc2-reactor-bridge-test': 'de95ca2e1932458f12b0b1cd4e9b4261444f41517219f21b375aa1072adfe0f1',
    'libco': '14288a4a73c339b039dd89597618ebb05702f90d668b76f74019d5d15ce67403',
}
SUMS = ''.join(PINS[n]+'  '+n+'\n' for n in ['cc2-reactor-bridge-test','launcher','libcc2-reactor-v2.so','preflight.sh']).replace('  launcher\n','  launcher.sh\n').encode()
PINS['sums'] = hashlib.sha256(SUMS).hexdigest()

# Exact shipped generations; never accept a mix of their runtime files.
PROFILES = {'V4.4.2': dict(PINS), 'V4.4.1': dict(PINS, **{
    'libcc2-reactor-v2.so': 'de6d86c4fa32aae05440a803099c553af934a22b25c6644d8ec977decc22c38a',
    'cc2-reactor-bridge-test': 'ece04de527fa39f7be4bb7f5b1378fc173b4c0e1151b7e8316dd281250507b43',
    'sums': 'd0f95aa4cde3844f4bfa3ca7fd27780dc355cf94f44740ab723cf26e1e59025e',
})}

def profile_sums(pins):
    return ''.join(pins[n]+'  '+n+'\n' for n in ('cc2-reactor-bridge-test','launcher','libcc2-reactor-v2.so','preflight.sh')).replace('  launcher\n','  launcher.sh\n').encode()

def select_profile(shipped, generation):
    for name, pins in PROFILES.items():
        if shipped == profile_sums(pins) and generation.strip() == pins['sums'].encode():
            return name, pins
    raise ValueError('Firmware/runtime source generation is not qualified')
MODES = {'boot': 0o755, 'launcher': 0o700, 'sums': 0o600, 'program': 0o700}

def digest(data):
    return hashlib.sha256(data).hexdigest()

def pinned(key, data, pins=PINS):
    if digest(data) != pins[key]:
        raise ValueError('Unsupported or changed component: '+key)

def patched_files(original, pins=PINS):
    for key in TARGETS:
        pinned(key, original[key], pins)
    launcher = original['launcher'].replace(ORIGINAL_SHA256.encode(), EXTENDED_SHA256.encode())
    if original['launcher'].count(ORIGINAL_SHA256.encode()) != 1:
        raise ValueError('Unexpected runtime launcher')
    old = b'[ "$(sha256sum "$BASE/elegoo_printer" | awk \'{print $1}\')" = "$EXPECTED" ] || fallback "$@"'
    new = old.replace(b'"$EXPECTED"', ('"'+EXTENDED_SHA256+'"').encode())
    if original['boot'].count(old) != 1:
        raise ValueError('Unexpected firmware boot hook')
    return {'boot': original['boot'].replace(old, new), 'launcher': launcher,
            'sums': original['sums'].replace(pins['launcher'].encode(), digest(launcher).encode()),
            'program': transform(original['program'])}

def clamp_config(data):
    """Change only numeric unload_fila_dist tokens in ordinary or SAVE_CONFIG canvas sections.
    Reject ambiguous/malformed values. Both config layers are inspected separately.
    """
    if len(data) > 256*1024 or b'\0' in data:
        raise ValueError('Unsupported configuration size/content')
    sections = {False: False, True: False}
    out, values = [], []
    for line in data.splitlines(keepends=True):
        saved = line.startswith(b'#*#')
        prefix = b'#*#' if saved else b''
        body = line[3:] if saved else line
        code = body.split(b'#',1)[0].strip()
        if code.startswith(b'[include '):
            raise ValueError('Included configuration files are not supported by this pilot')
        if code.startswith(b'['):
            if not re.fullmatch(rb'\[[^\r\n\[\]]+\]',code):
                raise ValueError('Ambiguous configuration section')
            sections[saved] = code == b'[canvas_dev]'
        elif sections[saved] and re.match(rb'^unload_fila_dist(?:\s|=|:|$)',code):
            match = re.fullmatch(rb'(\s*unload_fila_dist\s*[=:]\s*)([^#\r\n]*?)([ \t]*(?:#[^\r\n]*)?)(\r?\n)?',body)
            if not match:
                raise ValueError('Malformed pull-back entry')
            raw = match[2].strip()
            if not re.fullmatch(rb'[+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?',raw):
                raise ValueError('Unsupported pull-back value')
            try:
                number = Decimal(raw.decode('ascii'))
            except InvalidOperation as error:
                raise ValueError('Invalid pull-back value') from error
            if not number.is_finite() or not 60 <= number <= 1000:
                raise ValueError('Pull-back value outside qualified bounds')
            values.append(number)
            if number > 350:
                line = prefix+match[1]+b'350'+match[3]+(match[4] or b'')
        out.append(line)
    result = b''.join(out)
    return result, values

def validate_tar_member(member):
    path = PurePosixPath(member.name)
    if path.is_absolute() or '..' in path.parts or not member.isfile() or member.size > 32*1024*1024:
        raise ValueError('Unsupported snapshot member: '+member.name)
    return '/'+str(path)
