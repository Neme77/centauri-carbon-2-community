import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'

// The printer program cannot open a file of 2 GiB or more (32-bit stat), so the server refuses such a start before the
// copy (reason file_too_large_for_printer); the page says so plainly instead of showing the server's English text.
const dialog = readFileSync(new URL('../src/pages/print-dialog.tsx', import.meta.url), 'utf8')
assert.match(dialog, /reason === 'file_too_large_for_printer'/, 'the print dialog recognises a start refused as too large for the printer')
assert.match(dialog, /print\.file_too_large_for_printer/, 'and shows its own message')
for (const lang of ['en', 'de', 'fr', 'es', 'it', 'pl', 'cs', 'hu', 'tr', 'ru', 'zh']) {
  const strings = JSON.parse(readFileSync(new URL(`../public/locales/${lang}.json`, import.meta.url), 'utf8'))
  assert.ok((strings['print.file_too_large_for_printer'] ?? '').length > 20, `${lang}: the message is translated`)
}
console.log('PASS start of a file over 2 GiB: own message in every language')
