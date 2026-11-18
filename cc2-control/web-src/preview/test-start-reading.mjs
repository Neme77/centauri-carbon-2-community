import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'

// The printer refuses to analyse a file that a print start is still reading (reason start_reading), so the stick is
// never read twice at once; the page says so plainly instead of showing the server's English text.
const dialog = readFileSync(new URL('../src/pages/print-dialog.tsx', import.meta.url), 'utf8')
assert.match(dialog, /reason === 'start_reading'/, 'the print dialog recognises an analysis refused while a start reads')
assert.match(dialog, /print\.start_reading/, 'and shows its own message')
for (const lang of ['en', 'de', 'fr', 'es', 'it', 'pl', 'cs', 'hu', 'tr', 'ru', 'zh']) {
  const strings = JSON.parse(readFileSync(new URL(`../public/locales/${lang}.json`, import.meta.url), 'utf8'))
  assert.ok((strings['print.start_reading'] ?? '').length > 20, `${lang}: the message is translated`)
}
console.log('PASS analysis during a start of the same file: own message in every language')
