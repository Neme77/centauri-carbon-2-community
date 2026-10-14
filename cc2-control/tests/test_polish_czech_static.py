"""The new locales must be complete, UTF-8 and preserve interpolation variables."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / 'web-src/public/locales'
source = json.loads((DIRECTORY / 'en.json').read_text(encoding='utf-8'))
for language in ('pl', 'cs'):
    translated = json.loads((DIRECTORY / f'{language}.json').read_text(encoding='utf-8'))
    assert set(translated) == set(source), f'{language}: incomplete locale'
    for key, text in translated.items():
        assert text.strip() and '\ufffd' not in text, (language, key)
        assert sorted(re.findall(r'\{[^{}]+\}', text)) == sorted(re.findall(r'\{[^{}]+\}', source[key])), (language, key)
    assert translated == json.loads((ROOT / f'web/locales/{language}.json').read_text(encoding='utf-8'))
print('PASS: Polish and Czech cover every source key and preserve placeholders')
