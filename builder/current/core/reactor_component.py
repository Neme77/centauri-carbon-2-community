"""Install and audit the experimental O0 runtime in an extracted firmware tree."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct

FILES=('libcc2-reactor-v2.so','cc2-reactor-bridge-test','launcher.sh','preflight.sh','SHA256SUMS')
VENDOR_SHA256='c21126e78a63ac9140e7347c56f363d5e513495c58c06fc17e8ef97846f3c0a3'
LIBCO_SHA256='14288a4a73c339b039dd89597618ebb05702f90d668b76f74019d5d15ce67403'
FIRMWARE_LAUNCHER_SHA256='7e88f889d941eba70c2ec19de593a4aea0064af71914497633070397f4a7f700'

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def load(component):
    component=Path(component)
    if digest(component/'firmware-launcher.sh')!=FIRMWARE_LAUNCHER_SHA256:
        raise RuntimeError('Firmware reactor boot hook differs from pinned source')
    manifest=json.loads((component/'prepared-manifest.json').read_text())
    if manifest.get('optimization')!='O0' or manifest.get('gcc')!='6.5.0' or manifest.get('glibc')!='2.23':
        raise RuntimeError('Reactor requires qualified O0/GCC 6.5.0/glibc 2.23')
    if manifest.get('vendor_sha256')!=VENDOR_SHA256 or manifest.get('libco_sha256')!=LIBCO_SHA256:
        raise RuntimeError('Reactor ABI target differs from qualified firmware')
    if set(manifest.get('files',{}))!=set(FILES):
        raise RuntimeError('Unexpected reactor component files')
    repo=Path(__file__).resolve().parents[3]
    for name,expected in manifest.get('sources',{}).items():
        if name not in ('reactor_module.cpp','bridge_test.cpp','co_routine.h','fingerprints.h'):
            raise RuntimeError('Unexpected reactor source')
        if digest(repo/'experimental/combined-test/reactor'/name)!=expected:
            raise RuntimeError('Stale reactor build: source changed')
    if len(manifest.get('sources',{}))!=4:
        raise RuntimeError('Missing reactor source provenance')
    for name,expected in manifest['files'].items():
        if digest(component/'prepared'/name)!=expected:
            raise RuntimeError('Reactor checksum mismatch: '+name)
    for name in FILES[:2]:
        data=(component/'prepared'/name).read_bytes()
        if len(data)<20 or data[:6]!=b'\x7fELF\x01\x01' or struct.unpack_from('<H',data,18)[0]!=40:
            raise RuntimeError('Reactor component is not ARM32: '+name)
    for name in ('launcher.sh','preflight.sh'):
        canonical=repo/'experimental/combined-test'/name
        if digest(canonical)!=manifest['files'][name]:
            raise RuntimeError('Stale reactor launcher: '+name)
    expected=''.join(manifest['files'][name]+'  '+name+'\n' for name in sorted(FILES) if name!='SHA256SUMS')
    if (component/'prepared/SHA256SUMS').read_text()!=expected:
        raise RuntimeError('Reactor checksum list differs from manifest')
    return component,manifest

def patched_vendor_launcher(text):
    pattern=r'(?m)^(\s*(?:LD_BIND_NOW=1\s+)?)elegoo_printer(?=\s)'
    result,count=re.subn(pattern,r'\1/etc/init.d/cc2-reactor-launch',text)
    if count!=1:
        raise RuntimeError('Expected exactly one normal vendor launch line')
    return result

def patched_printer_init(text):
    # Preparation completes before the GUI's original eight-second head start.
    pattern=r'(?m)^(?P<indent>[ \t]*)if \[ \$ec_gui_exist_cmd \];then[ \t]*$'
    matches=list(re.finditer(pattern,text))
    if len(matches)!=1 or 'cc2-reactor-launch' in text:
        raise RuntimeError('Expected one unmodified vendor GUI startup guard')
    match=matches[0]
    prefix=(match['indent']+'# Prepare experimental runtime before vendor GUI startup.\n'
            +match['indent']+'/etc/init.d/cc2-reactor-launch --prepare-only || logger -t cc2-reactor "Preparation failed; normal launcher will recheck"\n\n')
    return text[:match.start()]+prefix+text[match.start():]

def install(root,component):
    root=Path(root);component,manifest=load(component)
    if digest(root/'opt/bin/elegoo_printer')!=VENDOR_SHA256 or digest(root/'opt/lib/libcolib.so')!=LIBCO_SHA256:
        raise RuntimeError('Firmware reactor vendor/libco target mismatch')
    destination=root/'opt/inst/cc2-reactor';destination.mkdir(parents=True,exist_ok=True)
    for name in FILES:
        shutil.copyfile(component/'prepared'/name,destination/name)
        (destination/name).chmod(0o700 if name!='SHA256SUMS' else 0o600)
    hook=root/'etc/init.d/cc2-reactor-launch'
    shutil.copyfile(component/'firmware-launcher.sh',hook);hook.chmod(0o755)
    launcher=root/'opt/bin/run_printer.sh'
    original=launcher.read_text()
    (destination/'run_printer.original.sh').write_text(original)
    (destination/'run_printer.original.sh').chmod(0o600)
    launcher.write_text(patched_vendor_launcher(original));launcher.chmod(0o755)
    printer_init=root/'etc/init.d/printer'
    init_original=printer_init.read_text()
    init_patched=patched_printer_init(init_original)
    (destination/'printer-init.original.sh').write_text(init_original)
    (destination/'printer-init.original.sh').chmod(0o600)
    printer_init.write_text(init_patched)
    return manifest

def audit(root,component):
    root=Path(root);component,manifest=load(component)
    for name,expected in manifest['files'].items():
        if digest(root/'opt/inst/cc2-reactor'/name)!=expected:
            raise RuntimeError('Rebuilt reactor checksum mismatch: '+name)
    if digest(root/'etc/init.d/cc2-reactor-launch')!=digest(component/'firmware-launcher.sh'):
        raise RuntimeError('Rebuilt reactor boot hook mismatch')
    original=(root/'opt/inst/cc2-reactor/run_printer.original.sh').read_text()
    if (root/'opt/bin/run_printer.sh').read_text()!=patched_vendor_launcher(original):
        raise RuntimeError('Rebuilt vendor launch line mismatch')
    for name in FILES[:-1]:
        if (root/'opt/inst/cc2-reactor'/name).stat().st_mode & 0o777 != 0o700:
            raise RuntimeError('Rebuilt reactor mode mismatch: '+name)
    if (root/'etc/init.d/cc2-reactor-launch').stat().st_mode & 0o777 != 0o755:
        raise RuntimeError('Rebuilt reactor boot hook mode mismatch')

    init_original=(root/'opt/inst/cc2-reactor/printer-init.original.sh').read_text()
    if (root/'etc/init.d/printer').read_text()!=patched_printer_init(init_original):
        raise RuntimeError('Rebuilt vendor GUI startup coordination mismatch')
