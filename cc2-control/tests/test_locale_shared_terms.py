#!/usr/bin/env python3
"""Reject English copies disguised as complete translations.

Key parity alone accepted whole RFID/queue sections copied from en.json. Only
reviewed shared terms may remain identical, pinned to the text AND locale so a
new English sentence cannot inherit an old exemption. This is a completeness
guard, not a substitute for native-speaker review of translated wording.
"""
import json
from pathlib import Path

from _websrc import LOCALES


def check_shared_terms(source, catalogs, exceptions):
    for key, entry in exceptions.items():
        assert key in source and entry['text'] == source[key], f'stale shared term: {key}'
        assert entry['reason'].strip(), f'missing shared-term reason: {key}'
        locales = entry['locales']
        assert locales and len(locales) == len(set(locales)), f'invalid locales: {key}'
        for code in locales:
            assert code in catalogs, f'unknown locale: {code}'
            assert catalogs[code].get(key) == source[key], f'unused shared term: {code}/{key}'
    for code, data in catalogs.items():
        for key, value in source.items():
            assert key in data, f'missing translation: {code}/{key}'
            if data[key].strip() == value.strip():
                assert code in exceptions.get(key, {}).get('locales', []), (
                    f'English text needs translation or a reviewed shared-term exception: {code}/{key}'
                )


def regression_checks():
    source = {'test.title': 'Scan tag', 'test.unit': 'RFID'}
    catalogs = {'fr': {'test.title': 'Scanner une étiquette', 'test.unit': 'RFID'}}
    exceptions = {'test.unit': {'text': 'RFID', 'locales': ['fr'], 'reason': 'Acronym'}}
    check_shared_terms(source, catalogs, exceptions)

    def rejected(s, c, e):
        try:
            check_shared_terms(s, c, e)
        except AssertionError:
            return
        raise AssertionError('regression fixture unexpectedly accepted')

    rejected(source, {'fr': {**catalogs['fr'], 'test.title': 'Scan tag'}}, exceptions)
    rejected(source, {'fr': {**catalogs['fr'], 'test.title': ' Scan tag '}}, exceptions)
    rejected(source, {'fr': {'test.unit': 'RFID'}}, exceptions)
    rejected({**source, 'test.unit': 'Scan an RFID tag'},
             {'fr': {**catalogs['fr'], 'test.unit': 'Scan an RFID tag'}}, exceptions)
    rejected(source, {**catalogs, 'pl': {'test.title': 'Skanuj tag', 'test.unit': 'RFID'}}, exceptions)
    rejected(source, {'fr': {**catalogs['fr'], 'test.unit': 'Identifiant radio'}}, exceptions)


if __name__ == '__main__':
    regression_checks()
    source = json.loads((LOCALES / 'en.json').read_text(encoding='utf-8'))
    catalogs = {p.stem: json.loads(p.read_text(encoding='utf-8'))
                for p in sorted(LOCALES.glob('*.json')) if p.stem != 'en'}
    exceptions = json.loads(Path(__file__).with_name('locale_shared_terms.json').read_text(encoding='utf-8'))
    check_shared_terms(source, catalogs, exceptions)
    print(f'PASS: {len(catalogs)} locales; English copies rejected except reviewed shared terms; six negative fixtures')
