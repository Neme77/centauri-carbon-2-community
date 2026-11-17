import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'

// A file of 2 GiB or more (PR #390 review): the operator decides. The question names the estimated copy time and the
// internal memory a USB copy takes, from the measured 2.5 GiB run (434.5 s); the start and the queued job carry the
// confirmation line "large_ok" (without it the server refuses: file_too_large_for_printer); after a confirmed start
// the dialog stays open until CC2 Control knows whether the printer began it (status print_watch).
const { copyEstimate, MEASURED_COPY_BYTES, MEASURED_COPY_SECONDS, LARGE_OK } = await import('../src/lib/printer-file-limit.ts')
assert.equal(MEASURED_COPY_BYTES, 2684354560, 'the measured run: 2.5 GiB')
assert.equal(MEASURED_COPY_SECONDS, 434.5, 'took 434.5 s')
assert.deepEqual(copyEstimate(2684354560), { minutes: 7, gib: 2.5 }, '2.5 GiB: about 7 min, 2.5 GiB')
assert.deepEqual(copyEstimate(2147483648), { minutes: 6, gib: 2 }, '2 GiB: about 6 min')
assert.deepEqual(copyEstimate(4294967295), { minutes: 12, gib: 4 }, 'the FAT limit: about 12 min')
assert.equal(LARGE_OK, 'large_ok')
const read = file => readFileSync(new URL(`../src/${file}`, import.meta.url), 'utf8')
const warning = read('lib/large-file-warning.ts')
assert.match(warning, /storage === 'usb'/, 'only a USB file is copied, so only it gets the estimate')
assert.match(warning, /files\.large_copy_estimate/, 'with the estimate text')
assert.match(warning, /files\.large_not_opened/, 'and the note that the printer has not opened such files')
const dialog = read('pages/print-dialog.tsx')
assert.match(dialog, /\$\{large \? `\\n\$\{LARGE_OK\}` : ''\}/, 'the start sends the ninth line large_ok when confirmed')
assert.match(dialog, /largeOk: large/, 'and the queued job keeps the confirmation')
assert.match(dialog, /reasonOf\(err\) !== 'file_too_large_for_printer'/, 'a refusal for an unknown size asks and retries')
assert.match(dialog, /typeof result\?\.watch === 'number'/, 'a watched start keeps the dialog open')
assert.match(dialog, /print_watch/, 'it follows the status print_watch')
assert.match(dialog, /print\.large_start_failed/, 'and says when the printer did not open the file')
assert.match(read('lib/queue.ts'), /a\.largeOk \? \[LARGE_OK\] : \[\]/, 'the queue request carries it as its last line, after the filaments')
assert.match(read('pages/queue.tsx'), /'not_opened'[\s\S]*queue\.not_opened/, 'the queue shows a start the printer did not open')
for (const lang of ['en', 'de', 'fr', 'es', 'it', 'pl', 'cs', 'hu', 'tr', 'ru', 'zh']) {
  for (const dir of ['../public/locales', '../../web/locales']) {
    const s = JSON.parse(readFileSync(new URL(`${dir}/${lang}.json`, import.meta.url), 'utf8'))
    assert.match(s['files.large_copy_estimate'] ?? '', /\{minutes\}[\s\S]*\{gib\}/, `${lang}: estimate names minutes and GiB`)
    for (const key of ['files.large_not_opened', 'print.large_start_watching', 'print.large_start_failed', 'queue.not_opened'])
      assert.ok((s[key] ?? '').length > 10, `${lang}: ${key}`)
    assert.match(s['print.large_start_failed'], /2/, `${lang}: the failure names the 2 GiB`)
  }
}
console.log('PASS files of 2 GiB or more: estimate, confirmation line, watched start, queue result, every language')
