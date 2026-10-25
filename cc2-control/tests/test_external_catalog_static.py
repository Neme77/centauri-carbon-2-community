#!/usr/bin/env python3
"""The browser choices and protected backend must use identical native facts."""
import json,pathlib,re
root=pathlib.Path(__file__).resolve().parents[1]
catalog=json.loads((root/'web-src/src/lib/external-filament-catalog.json').read_text())
text=(root/'src/external_filament_catalog.h').read_text()
rows=re.findall(r'^\{(\d+),(\d+),(\d+),("[^"\n]+"),("[^"\n]+"),("[^"\n]+"),(\d+),(\d+),(\d+)\}',text,re.M)
expected=[]
for bi,b in enumerate(catalog['brands']):
 for ti,t in enumerate(b['types']):
  for si,s in enumerate(t['items']):expected.append((bi,ti,si,b['name'],t['name'],s['name'],s['min'],s['max'],s['code']))
actual=[tuple([int(x[0]),int(x[1]),int(x[2]),json.loads(x[3]),json.loads(x[4]),json.loads(x[5]),int(x[6]),int(x[7]),int(x[8])]) for x in rows]
assert actual==expected
assert len(expected)==84 and len(catalog['colors'])==24
assert len(set(catalog['colors']))==24
print('PASS: 84 native material combinations, temperatures/codes and original 24-color palette stay in sync')
