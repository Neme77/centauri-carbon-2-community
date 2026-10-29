"""Apply the experimental status adapter to a disposable CC2 source checkout.

Not a printer installer. Canonical sources/snapshot stay untouched on this
feature branch until the full network editor is ready for its own PR.
"""
from pathlib import Path
import shutil
import sys

source=Path(__file__).resolve().parent
app=Path(sys.argv[1]).resolve()
main=app/'src/main.c'
text=main.read_text(encoding="utf-8")
assert text.count('#include "backup.h"')==1
assert text.count('if(!strcmp(method,"GET")&&!strcmp(path,"/api/bed-drying"))')==1
text=text.replace('#include "backup.h"','#include "backup.h"\n#include "network_status_http.h"')
text=text.replace('if(!strcmp(method,"GET")&&!strcmp(path,"/api/bed-drying")){',
    '''if(!strcmp(path,"/api/network")){
        if(!strcmp(method,"GET"))network_status_response(fd);
        else {
            const char *error="{\\"error\\":\\"Network status is read-only\\"}\\n";
            respond(fd,405,"Method Not Allowed","application/json; charset=utf-8",error,strlen(error));
        }
    } else if(!strcmp(method,"GET")&&!strcmp(path,"/api/bed-drying")){''')
main.write_bytes(text.encode("utf-8"))
shutil.copyfile(source/'http_status.h',app/'src/network_status_http.h')
shutil.copyfile(source/'status.c',app/'src/network_status.c')
shutil.copyfile(source/'status_core.h',app/'src/status_core.h')
make=app/'Makefile'
text=make.read_text(encoding="utf-8")
old='src/panda.c src/uds.c -o $@'
assert text.count(old)==1
text=text.replace(old,'src/panda.c src/uds.c src/network_status.c -DCC2_NETWORK_STATUS_LIBRARY -o $@')
make.write_bytes(text.encode("utf-8"))
print('Experimental GET-only /api/network adapter applied to',app)
