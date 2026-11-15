import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'

// The page waits for a print start as long as the file needs: 300 s at least, more for a large file
// (Baustein 7 b). Without a size, as before, 300 s. Both start paths pass the size they know.
const { startTimeoutMs } = await import('../src/lib/start-timeout.ts')
assert.equal(startTimeoutMs(), 300000, 'no size: 300 s')
assert.equal(startTimeoutMs(null), 300000, 'unknown size: 300 s')
assert.equal(startTimeoutMs(75_000_000), 300000, 'small file: 300 s')
assert.equal(startTimeoutMs(500_000_000), 370000, '500 MB: 120 s + 250 s')
assert.equal(startTimeoutMs(1_200_000_000), 720000, '1.2 GB: 120 s + 600 s')
assert.ok(startTimeoutMs(2_000_000_000) > startTimeoutMs(1_200_000_000), 'grows with the size')
// An upper bound: a hung stick or a huge file never keeps the page waiting longer than 20 minutes.
assert.equal(startTimeoutMs(4_000_000_000), 1200000, '4 GB: capped at 20 min')
assert.equal(startTimeoutMs(Number.MAX_SAFE_INTEGER), 1200000, 'any size: at most 20 min')
const read = file => readFileSync(new URL(`../src/${file}`, import.meta.url), 'utf8')
assert.match(read('pages/print-dialog.tsx'), /startTimeoutMs\(job\.size\)/, 'the print dialog passes the file size')
assert.match(read('lib/queue.ts'), /startTimeoutMs\(size\)/, 'the queue start passes the job size')
assert.match(read('pages/files.tsx'), /startFile\(e\.storage, e\.file\.path, e\.file\.size\)/, 'the file list hands the size on')
// When the wait runs out the page says so plainly, in every language, instead of a bare abort.
assert.match(read('lib/api.ts'), /print\.start_no_answer/, 'a start that runs out of time gets its own message')
for (const lang of ['en', 'de', 'fr', 'es', 'pl', 'cs', 'hu', 'tr', 'ru', 'zh']) {
  const strings = JSON.parse(readFileSync(new URL(`../../web/locales/${lang}.json`, import.meta.url), 'utf8'))
  assert.match(strings['print.start_no_answer'] ?? '', /\{minutes\}/, `${lang}: the message names the minutes`)
}
console.log('PASS start timeout follows the file size; dialog and queue pass it')
