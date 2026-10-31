"""Production route builder with a strict ip fixture; no real network changes."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parent
fixture=r'''#!/usr/bin/python3
import sys,json
from pathlib import Path
base=Path(__file__).parent
state=base/'routes.json'
s=json.loads(state.read_text()) if state.exists() else {'rules':[],'tables':{}}
a=sys.argv[1:]
assert a.pop(0)=='-4'
if 'table' in a:
 t=a[a.index('table')+1]
 if not t.isdigit() or not 1<=int(t)<=255:
  print("ip: invalid argument '%s' to 'table'"%t);sys.exit(1)
if a==['rule','show']:
 print('0: from all lookup prelocal\n1: from all lookup local')
 for pr,source,t in sorted(s['rules'],key=lambda r:int(r[0])):print(f'{pr}: from {source} lookup {t}')
 print('32766: from all lookup main\n32767: from all lookup default')
elif a[:2]==['route','show']:
 assert a[2:3]==['table'];print('\n'.join(s['tables'].get(a[3],[])),end='')
elif a[:2]==['route','replace']:
 t=a[a.index('table')+1];s['tables'].setdefault(t,[]).append(' '.join(a[2:]));state.write_text(json.dumps(s))
elif a[:2]==['route','flush']:
 assert a[2:3]==['table'];s['tables'][a[3]]=[];state.write_text(json.dumps(s))
elif a[:2] in (['rule','add'],['rule','del']):
 assert a[2]=='priority' and a[4]=='from' and a[6]=='table'
 r=[a[3],a[5].removesuffix('/32'),a[7]]
 if a[1]=='add':
  assert r not in s['rules'];s['rules'].append(r)
 else:s['rules'].remove(r)
 state.write_text(json.dumps(s))
else:raise AssertionError(a)
'''
with tempfile.TemporaryDirectory(prefix='cc2-routing-regression-') as tmp:
 base=Path(tmp);tools=base/'tools';tools.mkdir()
 (tools/'ip').write_text(fixture);(tools/'ip').chmod(0o700)
 binary=base/'routes-test'
 flags=['-std=c11','-Os','-Wall','-Wextra','-Wpedantic','-Werror','-D_FORTIFY_SOURCE=3','-DNG_TEST']
 flags += [f'-DNG_{k}="{v}"' for k,v in {'STATE':base/'state','RUN':base/'run','TEST_TOOLS':tools}.items()]
 subprocess.run(['gcc',*flags,root/'test_routes.c','-lm','-o',binary],check=True)
 subprocess.run([binary],check=True,timeout=20)
 log=(base/'state/action.log').read_text()
 assert 'ip_failed: -4 route show table 22050' in log
 assert "ip_output:ip: invalid argument '22050'" in log
print('PASS: persistent exact ip failure diagnostic and route regression; no kernel interface modified')
