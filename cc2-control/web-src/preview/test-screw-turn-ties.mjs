// Turning instruction: exact ties (20 um, 180 degrees) and half micrometres in the height text.
// The values come from the page's own subtraction of two six-decimal heights, which carries noise of about 1e-16 mm.
import assert from 'node:assert/strict'
import { heightLabel, SCREW_KEYS, screwPlan, turnPlan } from '../src/lib/screws.ts'

const PITCH = 700
const mm = um => (um / 1000).toFixed(6)
const heights = zs => Object.fromEntries(SCREW_KEYS.map((k, i) => [k, Number(zs[i])]))
// What the page does for front right: height difference, then the turning instruction for the opposite move.
const stageAt = (base, dev) => {
  const d = screwPlan(heights([mm(base), mm(base + dev), mm(base), mm(base)])).shown[1]
  return turnPlan(-d * 1000, PITCH).stage
}
const failed = []
const section = (n, fn) => {
  try {
    fn()
  } catch (e) {
    failed.push(`part ${n}: ${e.message.split('\n')[0]}`)
  }
}
section(1, () => {
  for (let base = -2000; base <= 2000; base++)
    for (const dev of [20, -20]) assert.equal(stageAt(base, dev), 0, `${base} ${dev}: exactly 20 um is within tolerance`)
})
section(2, () => {
  for (let base = -2000; base <= 2000; base++)
    for (const dev of [350, -350]) assert.equal(stageAt(base, dev), 2, `${base} ${dev}: exactly 180 degrees is one go`)
})
section(3, () => {
  assert.equal(stageAt(0, 351), 3)
  assert.equal(stageAt(0, 21), 1)
  assert.equal(turnPlan(20.000000000000018, PITCH).stage, 0)
  assert.equal(turnPlan(-20.000000000000018, PITCH).stage, 0)
  assert.equal(turnPlan(350.00000000000006, PITCH).stage, 2)
})
section(4, () => {
  for (let j = -4001; j <= 4001; j += 2) {
    const um = j / 2
    const mmv = Number((um / 1000).toFixed(6))
    assert.equal(heightLabel(mmv, 'um', true), `${um > 0 ? '+' : '−'}${Math.ceil(Math.abs(um))} µm`, `half micrometre ${um}`)
  }
  assert.equal(heightLabel(-0.0004, 'um', true), '0 µm')
  assert.equal(heightLabel(-0.0005, 'mm', true, 'de'), '−0,001 mm')
  assert.equal(heightLabel(0.0005, 'mm', false, 'en'), '0.001 mm')
})
if (failed.length) {
  console.error(failed.join('\n'))
  process.exit(1)
}
console.log('screw turn ties: ok')
