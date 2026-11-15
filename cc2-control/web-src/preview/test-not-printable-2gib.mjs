import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'

// The ELEGOO printer program opens only files below 2 GiB (32-bit stat). CC2 Control does not block a larger file:
// the page warns in the file list, in the details and before the start (the operator can cancel or go on).
// The wording is one text per language (files.not_printable_2gib).
const src = p => readFileSync(new URL(`../src/${p}`, import.meta.url), 'utf8')
const limit = src('lib/printer-file-limit.ts')
assert.match(limit, /PRINTER_FILE_LIMIT = 2147483648\b/, 'the limit is 2 GiB (2 147 483 648 bytes)')
assert.match(limit, /size >= PRINTER_FILE_LIMIT/, 'from 2 GiB on, not above it')
const files = src('pages/files.tsx')
assert.equal((files.match(/tooLargeForPrinter\(/g) || []).length, 2, 'the list row and the details both warn')
assert.match(files, /t\('files\.not_printable_2gib'\)/, 'with the one warning text')
const dialog = src('pages/print-dialog.tsx')
const start = dialog.slice(dialog.indexOf('export async function startFileOfSize'))
const warn = start.indexOf('tooLargeForPrinter(size)'), inspect = start.indexOf("post('/api/gcode-files/inspect'")
assert.ok(warn > 0 && warn < inspect, 'the start warns before anything is read or copied')
assert.match(start, /await ask\(`\$\{t\('files\.not_printable_2gib'\)\}/, 'and asks: cancel or start anyway')
assert.doesNotMatch(dialog + files, /file_too_large_for_printer/, 'no block: the server is not asked to refuse')
for (const lang of ['en', 'de', 'fr', 'es', 'it', 'pl', 'cs', 'hu', 'tr', 'ru', 'zh']) {
  for (const dir of ['../public/locales', '../../web/locales']) {
    const s = JSON.parse(readFileSync(new URL(`${dir}/${lang}.json`, import.meta.url), 'utf8'))
    assert.ok((s['files.not_printable_2gib'] ?? '').replace(/[\s.,\u00a0\u202f]/g, '').includes('2147483647'), `${lang}: the warning names the largest printable size`)
    assert.ok((s['files.not_printable'] ?? '').length > 2 && (s['files.print_anyway'] ?? '').length > 2, `${lang}: tag and question`)
  }
}
console.log('PASS files of 2 GiB or more: warned in list, details and before the start, not blocked, every language')
