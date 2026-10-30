"""Assemble into a disposable app directory; never edit canonical/generated UI."""
import json
from pathlib import Path
import shutil
import sys

root=Path(__file__).resolve().parent
previous=root.parent/'network-manager'
app=Path(sys.argv[1]).resolve()
sha=sys.argv[2]
assert len(sha)==64 and all(c in '0123456789abcdef' for c in sha)
target=app/'src/connections'
target.mkdir(exist_ok=True)
for file in root.glob('*.h'):
    if file.name!='http.h': shutil.copyfile(file,target/file.name)
(target/'pin.h').write_text('#define CC2_CONNECTIONS_HELPER_SHA "'+sha+'"\n')
shutil.copyfile(root/'http.h',app/'src/connections_http.h')
shutil.copyfile(previous/'http_status.h',app/'src/network_status_http.h')
shutil.copyfile(root/'status.c',app/'src/network_status.c')
shutil.copyfile(previous/'status_core.h',app/'src/status_core.h')
main=app/'src/main.c'
text=main.read_text()
assert '#include "connections_http.h"' not in text
text=text.replace('#include "backup.h"','#include "backup.h"\n#include "network_status_http.h"\n#include "connections_http.h"')
original='!emergency && app_update_active())'
assert text.count(original)==1
text=text.replace(original,'!emergency && (app_update_active() || (connections_busy() && strcmp(path,"/api/network/action"))))')
start='if(!strcmp(method,"GET")&&!strcmp(path,"/api/bed-drying"))'
assert text.count(start)==1
text=text.replace(start,'''if(!strcmp(method,"GET")&&!strcmp(path,"/api/network")) {
        network_status_response(fd);
    } else if(!strcmp(method,"GET")&&!strcmp(path,"/api/network/manager")) {
        connections_status(fd,mqtt,console);
    } else if(!strcmp(method,"POST")&&!strcmp(path,"/api/network/action")) {
        connections_action(fd,body,body_len,0,mqtt,console);
    } else if(!strcmp(method,"POST")&&!strcmp(path,"/api/network/scan")) {
        connections_action(fd,body,body_len,1,mqtt,console);
    } else if(!strcmp(path,"/api/network/select") || !strcmp(path,"/api/network/trial")) {
        app_update_fail(fd,410,"Reload CC2 Control to use the Connections manager");
    } else '''+start)
main.write_text(text,newline='\n')
make=app/'Makefile'
text=make.read_text()
assert text.count('src/panda.c src/uds.c -o $@')==1
text=text.replace('src/panda.c src/uds.c -o $@','src/panda.c src/uds.c src/network_status.c -DCC2_NETWORK_STATUS_LIBRARY -o $@')
make.write_text(text,newline='\n')
shutil.copyfile(root/'Network.tsx',app/'web-src/src/pages/Network.tsx')
strings=json.loads((root/'ui-strings.json').read_text())
for language,entries in strings.items():
    path=app/'web-src/public/locales'/f'{language}.json'
    data=json.loads(path.read_text())
    data.update(entries)
    path.write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n',encoding='utf-8',newline='\n')
print('Connections manager, typed asynchronous actions and existing authorization preserved:',app)
