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
assert.ok(startTimeoutMs(4_000_000_000) > startTimeoutMs(1_200_000_000), 'grows with the size')
const read = file => readFileSync(new URL(`../src/${file}`, import.meta.url), 'utf8')
assert.match(read('pages/print-dialog.tsx'), /startTimeoutMs\(job\.size\)/, 'the print dialog passes the file size')
assert.match(read('lib/queue.ts'), /startTimeoutMs\(size\)/, 'the queue start passes the job size')
assert.match(read('pages/files.tsx'), /startFile\(e\.storage, e\.file\.path, e\.file\.size\)/, 'the file list hands the size on')
console.log('PASS start timeout follows the file size; dialog and queue pass it')
