"""Install experimental network UI in a disposable application checkout."""
import json
from pathlib import Path
import re
import shutil
import sys

source=Path(__file__).resolve().parent
app=Path(sys.argv[1]).resolve()
web=app/'web-src'
shutil.copyfile(source/'Network.tsx',web/'src/pages/Network.tsx')
settings=web/'src/pages/settings.tsx'
text=settings.read_text(encoding="utf-8")
assert text.count("import { Experiments } from './Experiments'")==1
text=text.replace("import { Experiments } from './Experiments'","import { Experiments } from './Experiments'\nimport { Network } from './Network'")
assert text.count("[Link, 'settings.connection'],")==1
text=text.replace("[Link, 'settings.connection'],","[Link, 'network.connections'],")
old='<CardHead icon="link" title="settings.connection" sub="settings.network_and_device_access_settings" />'
assert text.count(old)==1
text=text.replace(old,'<CardHead icon="link" title="network.cc2_access" sub="settings.network_and_device_access_settings" />')
old='{tab === 0 && <Connection />}'
assert text.count(old)==1
text=text.replace(old,'{tab === 0 && <div class="grid gap-3.5"><Network /><Connection /></div>}')
settings.write_bytes(text.encode("utf-8"))
icons=web/'src/components/icons.tsx'
text=icons.read_text(encoding="utf-8")
text=text.replace('  Zap,','  Zap,\n  Wifi,\n  EthernetPort,')
text=text.replace('  home: House,','  wifi: Wifi,\n  ethernet: EthernetPort,\n  home: House,')
icons.write_bytes(text.encode("utf-8"))
# The existing mesh uses an always-dark inset even in the light theme. Its
# caption must use the inset foreground, not the light-page muted token.
bed=web/'src/pages/bed.tsx'
text=bed.read_text(encoding='utf-8')
old='<div class="p-2 text-center text-xs text-muted">'
assert text.count(old)==1
text=text.replace(old,'<div class="p-2 text-center text-xs text-well-fg/70">')
text=text.replace('<p class="text-muted">{t(\'bed.waiting_for_live_mesh_data_2\')}</p>',
                  '<p class="text-well-fg/70">{t(\'bed.waiting_for_live_mesh_data_2\')}</p>')
bed.write_bytes(text.encode('utf-8'))
strings=json.loads((source/'ui-strings.json').read_text(encoding="utf-8"))
assert set(strings)=={'en','it','fr','zh','ru','pl','cs','hu','es'}
for lang,dictionary in strings.items():
    assert set(dictionary)==set(strings['en'])
    path=web/'public/locales'/f'{lang}.json'
    current=json.loads(path.read_text(encoding="utf-8"))
    for key,value in dictionary.items():
        assert key.startswith('network.') and key not in current
        current[key]=value
    path.write_bytes((json.dumps(current,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
print('Network tab integrated into disposable UI with nine dictionaries:',app)

