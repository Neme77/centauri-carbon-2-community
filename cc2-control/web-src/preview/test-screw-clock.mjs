// Clock-face turn instruction ("CW 01:20") for the screw alignment: the format, the rounding and the
// thread-type table follow Klipper's SCREWS_TILT_CALCULATE. Klipper itself is not run here: the golden table
// holds the formula recomputed by hand from klippy/extras/screws_tilt_adjust.py, next to the expected output
// of this implementation, worked out independently. Where the two differ on purpose, the row says why.
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import {
  ALIGN_DEFAULTS,
  ALIGN_NOOP_MM,
  analyseMesh,
  clockLabel,
  clockText,
  clockTurn,
  parseAlignConfig,
  screwClock,
  turnDirection,
} from '../src/lib/mesh-align.ts'
import { syntheticMesh } from './mesh-align-synthetic.mjs'

const golden = JSON.parse(readFileSync(fileURLToPath(new URL('./screw-clock-golden.json', import.meta.url)), 'utf8'))
// Nothing configured (the old empty defaults); the CC2 defaults are checked separately below.
const BLANK = { ...ALIGN_DEFAULTS, thread_pitch_mm: null, thread_pitch_source: '', clockwise_raises_bed: null, rotation_source: '' }
const config = extra => ({ ...BLANK, ...extra })
const near = (a, b, eps, msg) => assert.ok(Math.abs(a - b) <= eps, `${msg}: ${a} vs ${b}`)

// Klipper's screw_thread names and the pitch of each size, for the golden table only (the page has no thread
// choice any more: it uses the CC2's own screw). CW means that turning clockwise raises the bed in Klipper's convention.
const THREAD_TYPES = ['CW-M3', 'CCW-M3', 'CW-M4', 'CCW-M4', 'CW-M5', 'CCW-M5', 'CW-M6', 'CCW-M6']
const THREAD_PITCH_MM = { 'CW-M3': 0.5, 'CCW-M3': 0.5, 'CW-M4': 0.7, 'CCW-M4': 0.7, 'CW-M5': 0.8, 'CCW-M5': 0.8, 'CW-M6': 1, 'CCW-M6': 1 }
const threadClockwiseRaises = type => type.startsWith('CW')

// The text of one instruction, as the screw list shows it.
const label = (move, type) => {
  const pitch = THREAD_PITCH_MM[type]
  const r = clockTurn(move, pitch)
  if (r.kind !== 'turn') return r.kind
  return clockLabel(r, turnDirection(move, threadClockwiseRaises(type)))
}

// Parity with the golden table: equal to Klipper where nothing is marked, and equal to the expected output everywhere.
let same = 0
let different = 0
for (const row of golden.rows) {
  const move = row.move_mm === 'NaN' ? Number.NaN : row.move_mm === 'Infinity' ? Infinity : row.move_mm === '-Infinity' ? -Infinity : row.move_mm
  const got = label(move, row.thread)
  assert.equal(got, row.ours, `${row.thread} ${row.move_mm}`)
  if (row.deviation === null) {
    assert.equal(got, row.klipper, `parity with Klipper: ${row.thread} ${row.move_mm}`)
    same++
  } else {
    assert.notEqual(got, row.klipper, `a marked deviation really differs: ${row.thread} ${row.move_mm}`)
    different++
  }
}
assert.ok(same >= 100 && different >= 60, `the table covers both: ${same} equal, ${different} different`)
const reasons = new Set(golden.rows.map(r => r.deviation))
for (const why of ['carry', 'rounding', 'below-one-minute', 'over-99', 'invalid']) assert.ok(reasons.has(why), `reason ${why}`)

// Worked values from the Klipper handbook example and from the analysis.
assert.equal(label(0.6667, 'CW-M3'), 'CW 01:20')
assert.equal(label(0.1, 'CW-M3'), 'CW 00:12')
assert.equal(label(-0.1, 'CW-M3'), 'CCW 00:12')
assert.equal(label(0.1, 'CCW-M4'), 'CCW 00:09')
assert.equal(label(-0.1, 'CCW-M4'), 'CW 00:09')

assert.deepEqual(clockTurn(0.5, 0.5), { kind: 'turn', turns: 1, minutes: 0 })
// A half minute always rounds up (Klipper's round() sends a half to the even number, and its floating-point sum
// can land a hair under it); floating-point noise just under a half must not drop the minute.
assert.deepEqual(clockTurn(0.0208333333333333, 0.5), { kind: 'turn', turns: 0, minutes: 3 })
assert.deepEqual(clockTurn(0.4958333333333333, 0.5), { kind: 'turn', turns: 1, minutes: 0 })
// Carry: never 60 minutes. 0.4999 mm on a 0.5 mm thread is one full turn, not "00:60".
assert.deepEqual(clockTurn(0.4999, 0.5), { kind: 'turn', turns: 1, minutes: 0 })
assert.deepEqual(clockTurn(0.5, 0.5), { kind: 'turn', turns: 1, minutes: 0 })
// Less than one minute, or no movement, is "nothing"; far too many turns is millimetres only; bad input is invalid.
assert.deepEqual(clockTurn(0, 0.5), { kind: 'none' })
assert.deepEqual(clockTurn(0.0007, 0.5), { kind: 'none' })
assert.deepEqual(clockTurn(0.004, 0.7), { kind: 'none' })
assert.deepEqual(clockTurn(49.9, 0.5), { kind: 'turn', turns: 99, minutes: 48 })
assert.equal(clockTurn(50, 0.5).kind, 'mm', '100 turns are not shown')
assert.equal(clockTurn(1e9, 0.5).kind, 'mm')
for (const bad of [[Number.NaN, 0.5], [Infinity, 0.5], [1, 0], [1, -0.5], [1, Number.NaN], [1, Infinity]]) {
  assert.equal(clockTurn(...bad).kind, 'invalid', `invalid ${bad}`)
}
assert.equal(clockText({ kind: 'turn', turns: 1, minutes: 20 }), '01:20')
assert.equal(clockText({ kind: 'turn', turns: 0, minutes: 5 }), '00:05')
assert.equal(clockLabel({ kind: 'turn', turns: 1, minutes: 20 }, 'cw'), 'CW 01:20')
assert.equal(clockLabel({ kind: 'turn', turns: 1, minutes: 20 }, 'ccw'), 'CCW 01:20')
assert.equal(clockLabel({ kind: 'turn', turns: 1, minutes: 20 }, null), '01:20', 'no direction: the amount alone')

// Properties over a fixed sweep: minutes stay below 60, the total grows with the distance, the mirror image
// swaps the direction, converting back is within half a minute, and the opposite thread swaps CW and CCW.
{
  let seed = 12345
  const rnd = () => ((seed = (seed * 1664525 + 1013904223) % 4294967296) / 4294967296)
  let previous = -1
  for (let i = 0; i <= 4000; i++) {
    const mm = i * 0.0013
    const r = clockTurn(mm, 0.7)
    if (r.kind !== 'turn') continue
    assert.ok(r.minutes >= 0 && r.minutes < 60, `minutes ${r.minutes}`)
    const total = r.turns * 60 + r.minutes
    assert.ok(total >= previous, 'the total never shrinks as the distance grows')
    previous = total
    near((total / 60) * 0.7, mm, 0.7 / 120 + 1e-9, 'converted back within half a minute')
  }
  for (let i = 0; i < 500; i++) {
    const mm = (rnd() - 0.5) * 40
    const pitch = [0.5, 0.7, 0.8, 1][Math.floor(rnd() * 4)]
    const up = clockTurn(mm, pitch)
    const down = clockTurn(-mm, pitch)
    assert.deepEqual(up, down, 'same amount either way')
    if (up.kind === 'turn') {
      assert.notEqual(turnDirection(mm, true), turnDirection(-mm, true), 'the mirror image swaps the direction')
      assert.notEqual(turnDirection(mm, true), turnDirection(mm, false), 'the opposite thread swaps it too')
    }
  }
}

// Mesh analysis: the clock follows the same gating as the turns and the direction.
{
  const grid = syntheticMesh({ tiltX: 0.48 })
  const none = analyseMesh(grid, BLANK)
  assert.ok(none.screws.every(s => s.clock === null), 'nothing set: no clock')
  const pitchOnly = analyseMesh(grid, config({ thread_pitch_mm: 0.7 }))
  assert.ok(pitchOnly.screws.every(s => s.clock === null), 'a pitch without a source gives no clock')
  const set = analyseMesh(
    grid,
    config({ thread_pitch_mm: 0.7, thread_pitch_source: 'measured', clockwise_raises_bed: false, rotation_source: 'wiki' })
  )
  const moved = set.screws.filter(s => !s.reference && Math.abs(s.correction) >= ALIGN_NOOP_MM)
  assert.ok(moved.length >= 1)
  for (const s of moved) {
    const expected = clockTurn(s.correction, 0.7)
    assert.equal(s.clock.turns, expected.turns)
    assert.equal(s.clock.minutes, expected.minutes)
    assert.equal(s.direction, s.correction > 0 === false ? 'cw' : 'ccw')
  }
  assert.ok(set.screws.filter(s => s.reference).every(s => s.clock === null), 'the reference screw has no clock')
}

// screwClock is what both tabs use. The four-point tab passes the move it needs: the negative of its difference.
{
  const full = config({ thread_pitch_mm: 0.7, thread_pitch_source: 'm', clockwise_raises_bed: false, rotation_source: 'w' })
  const d = 0.18 // this corner is 180 µm higher than the reference: lower it
  const lower = screwClock(-d, full)
  const mesh = screwClock(-d, full)
  assert.deepEqual(lower, mesh)
  assert.equal(lower.clock.turns * 60 + lower.clock.minutes, 15)
  assert.equal(lower.direction, 'cw', 'clockwise lowers the bed here, so lowering means clockwise')
  assert.deepEqual(screwClock(d, full).direction, 'ccw')
  assert.deepEqual(screwClock(-d, config({ thread_pitch_mm: 0.7 })), { clock: null, direction: null })
  const directionOnly = screwClock(-d, config({ clockwise_raises_bed: false, rotation_source: 'w' }))
  assert.equal(directionOnly.clock, null)
  assert.equal(directionOnly.direction, 'cw')
  const pitchOnly = screwClock(-d, config({ thread_pitch_mm: 0.7, thread_pitch_source: 'm' }))
  assert.equal(pitchOnly.clock.turns * 60 + pitchOnly.clock.minutes, 15)
  assert.equal(pitchOnly.direction, null)
  assert.deepEqual(screwClock(0.0003, full), { clock: null, direction: null }, 'below one minute: nothing')
}

// The CC2's own screw is fixed: M4, 0.7 mm per turn, clockwise from below raises the corner (measured) = CW-M4.
// Stored values of older versions (other pitch, direction, source, thread type) never change it.
{
  assert.equal(ALIGN_DEFAULTS.thread_pitch_mm, 0.7)
  assert.equal(ALIGN_DEFAULTS.clockwise_raises_bed, true)
  assert.equal('thread_preset' in ALIGN_DEFAULTS, false, 'no thread type is stored any more')
  assert.deepEqual(screwClock(0.7, ALIGN_DEFAULTS), { clock: screwClock(0.7, ALIGN_DEFAULTS).clock, direction: 'cw' }, 'raise 0.7 mm = one turn clockwise from below')
  assert.equal(clockLabel(screwClock(0.7, ALIGN_DEFAULTS).clock, 'cw'), 'CW 01:00')
  const fixed = c => ({
    pitch: c.thread_pitch_mm,
    pitch_source: c.thread_pitch_source,
    cw: c.clockwise_raises_bed,
    source: c.rotation_source,
  })
  const want = fixed(ALIGN_DEFAULTS)
  for (const stored of [
    null,
    '{"tilt_span_mm":200}',
    '{"thread_pitch_mm":0.5,"thread_pitch_source":"x","clockwise_raises_bed":false,"rotation_source":"Elegoo wiki","thread_preset":"CCW-M3"}',
    '{"thread_pitch_mm":null,"thread_pitch_source":"","clockwise_raises_bed":null,"rotation_source":"","thread_preset":null}',
    '{"thread_pitch_mm":9,"clockwise_raises_bed":"yes","thread_preset":"CW-M9"}',
  ]) {
    const parsed = parseAlignConfig(stored)
    assert.deepEqual(fixed(parsed), want, `a stored value does not override the CC2 screw: ${stored}`)
    assert.equal('thread_preset' in parsed, false)
  }
  assert.equal(parseAlignConfig('{"tilt_span_mm":200}').tilt_span_mm, 200, 'the other settings are still read')
}

console.log('PASS screw clock: Klipper format, carry and rounding, gating, fixed CC2 screw, both tabs')
