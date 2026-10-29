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
state=web/'src/lib/state.ts'
text=state.read_text(encoding="utf-8")
old="'connection' | 'integrations' | 'appearance' | 'about' | 'updates' | 'backup'"
assert text.count(old)==1
text=text.replace(old,"'connection' | 'network' | 'integrations' | 'appearance' | 'about' | 'updates' | 'backup'")
old="['connection', 'integrations', 'appearance', 'about', 'updates', 'backup']"
assert text.count(old)==1
text=text.replace(old,"['connection', 'network', 'integrations', 'appearance', 'about', 'updates', 'backup']")
state.write_bytes(text.encode("utf-8"))
settings=web/'src/pages/settings.tsx'
text=settings.read_text(encoding="utf-8")
assert text.count("import { Experiments } from './Experiments'")==1
text=text.replace("import { Experiments } from './Experiments'","import { Experiments } from './Experiments'\nimport { Network } from './Network'")
text=text.replace('Archive, FileDown }','Archive, FileDown, Wifi }')
assert text.count("[Link, 'settings.connection'],")==1
text=text.replace("[Link, 'settings.connection'],","[Link, 'settings.connection'],\n  [Wifi, 'network.title'],")
assert text.count(old)==1
text=text.replace(old,"['connection', 'network', 'integrations', 'appearance', 'about', 'updates', 'backup']")
text=re.sub(r'tab === ([1-5])',lambda m:'tab === '+str(int(m[1])+1),text)
text=text.replace('{tab === 0 && <Connection />}','{tab === 0 && <Connection />}\n          {tab === 1 && <Network />}')
settings.write_bytes(text.encode("utf-8"))
icons=web/'src/components/icons.tsx'
text=icons.read_text(encoding="utf-8")
text=text.replace('  Zap,','  Zap,\n  Wifi,\n  EthernetPort,')
text=text.replace('  home: House,','  wifi: Wifi,\n  ethernet: EthernetPort,\n  home: House,')
icons.write_bytes(text.encode("utf-8"))
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
