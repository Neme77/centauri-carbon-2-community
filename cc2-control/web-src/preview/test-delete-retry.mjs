import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'

// Right after a print is stopped the printer reports the job for up to ~2.7 s longer (measured on the printer,
// 5 of 5), so CC2 Control's guard refuses to delete with file_delete_active_print_unknown. The page tries again
// once a second for at most 5 s; the guard on the printer side stays exactly as it is.
const { deleteRetrying, DELETE_RETRY_MS } = await import('../src/lib/delete-retry.ts')
const reply = (status, code) => ({ ok: status < 300, status, clone() { return this }, json: async () => (code ? { code } : {}) })
const run = async replies => {
  const calls = [], waits = []
  const fetchFn = async () => { calls.push(1); return replies.shift() }
  const r = await deleteRetrying(fetchFn, '/api/gcode-files/delete', {}, async ms => { waits.push(ms) })
  return { r, calls: calls.length, waited: waits.reduce((a, b) => a + b, 0) }
}
let out = await run([reply(409, 'file_delete_active_print_unknown'), reply(409, 'file_delete_active_print_unknown'), reply(200)])
assert.equal(out.r.ok, true, 'deleted once the printer state is clear')
assert.equal(out.calls, 3, 'tried again twice')
out = await run(Array.from({ length: 20 }, () => reply(409, 'file_delete_active_print_unknown')))
assert.equal(out.r.ok, false, 'still unclear after the limit: the refusal is shown')
assert.ok(out.waited <= DELETE_RETRY_MS && DELETE_RETRY_MS <= 5000, 'never waits longer than 5 s')
out = await run([reply(409, 'file_delete_active_print'), reply(200)])
assert.equal(out.calls, 1, 'the active print itself is never retried')
out = await run([reply(404, 'file_delete_unsafe_file')])
assert.equal(out.calls, 1, 'other refusals are shown at once')
const files = readFileSync(new URL('../src/pages/files.tsx', import.meta.url), 'utf8')
assert.equal((files.match(/deleteRetrying\(/g) || []).length, 2, 'both delete paths of the file list use it')
console.log('PASS delete right after a stop: retried up to 5 s while the printer state is unclear, guard unchanged')
