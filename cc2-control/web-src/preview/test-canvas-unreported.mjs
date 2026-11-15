import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'

// A start the printer refuses because the Canvas did not report its trays (reason canvas_unreported) keeps the
// print dialog open with a plain message: the operator can start again, change the mapping or cancel there.
const read = file => readFileSync(new URL(`../src/${file}`, import.meta.url), 'utf8')
const dialog = read('pages/print-dialog.tsx')
assert.match(dialog, /reason === 'canvas_unreported'/, 'the print dialog recognises the Canvas refusal')
assert.match(dialog, /print\.canvas_unreported/, 'and shows its own message')
for (const lang of ['en', 'de', 'fr', 'es', 'it', 'pl', 'cs', 'hu', 'tr', 'ru', 'zh']) {
  const strings = JSON.parse(readFileSync(new URL(`../public/locales/${lang}.json`, import.meta.url), 'utf8'))
  assert.ok((strings['print.canvas_unreported'] ?? '').length > 20, `${lang}: the message is translated`)
}
console.log('PASS Canvas refusal: the dialog stays open with its own message in every language')
