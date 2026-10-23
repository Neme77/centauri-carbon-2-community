from pathlib import Path
import json
import shutil

root=Path(__file__).resolve().parents[2]
data=json.loads((Path(__file__).parent/'experimental-release-data.json').read_text())
for code,changes in data['locales'].items():
    assert code in ('en','it','fr','es','pl','cs','hu','ru','zh')
    path=root/'cc2-control/web-src/public/locales'/(code+'.json')
    dictionary=json.loads(path.read_text())
    dictionary.update(changes)
    path.write_bytes((json.dumps(dictionary,ensure_ascii=False,indent=2)+'\n').encode())
for name,text in data['notes'].items():
    assert name in ('README-TEST.md','README-TEST.it.md')
    (root/'cc2-control/installer'/name).write_bytes(text.encode())
print('Prepared release-channel translations and cumulative experimental operator notes.')
