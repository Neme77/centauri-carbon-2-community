// The strategy of the screw plan: front left stays the fixed reference by default, the optimized plan is only offered
// (canOptimize) and used after an explicit choice, and what was measured never changes with the strategy.
import assert from 'node:assert/strict'
import { measuredOffsets, parseScrewStrategy, SCREW_KEYS, screwPlan, screwTarget } from '../src/lib/screws.ts'

const heights = zs => Object.fromEntries(SCREW_KEYS.map((k, i) => [k, zs[i]]))
const um = mm => Math.round(mm * 1000)

// Front left is the outlier: the other three corners lie 100 to 150 µm above it.
const outlier = heights([1.8, 1.9, 1.95, 1.93])
const std = screwPlan(outlier)
assert.equal(std.strategy, 'standard', 'the default strategy is standard')
assert.equal(std.useOptimized, false, 'nothing switches to the optimized plan by itself')
assert.equal(std.canOptimize, true, 'the optimized plan is offered')
assert.deepEqual(std.shown.map(um), [0, 100, 150, 130], 'standard: front left stays 0, the others are shown against it')
assert.equal(std.shown[0], 0, 'front left keeps its reference')
assert.equal(screwTarget(outlier, std), 1.8, 'standard aims at the front-left height')
assert.deepEqual(screwPlan(outlier, 'standard').shown, std.shown, 'an explicit standard is the default')

const opt = screwPlan(outlier, 'optimized')
assert.equal(opt.useOptimized, true, 'the optimized plan follows the explicit choice')
assert.equal(opt.strategy, 'optimized')
assert.deepEqual(opt.shown.map(um), [-100, 0, 50, 30], 'optimized: front left moves by the common shift, the others follow')
assert.equal(screwTarget(outlier, opt), 1.8 + 0.1, 'optimized aims at front left + common')
// The plan carries both, so the page can name what the optimized plan would do while the standard one is shown.
assert.deepEqual(std.optimized.map(um), [-100, 0, 50, 30])
assert.deepEqual(opt.standard.map(um), [0, 100, 150, 130])
assert.equal(std.before, 150)
assert.equal(std.best, 100)

// What was measured does not depend on the strategy: always against front left (the old table showed the target after the turn).
assert.deepEqual(measuredOffsets(outlier).map(um), [0, 100, 150, 130])
assert.deepEqual(measuredOffsets(outlier), measuredOffsets(outlier))

// No automatic switching between measurements: whatever the heights, strategy 'standard' never shows the optimized plan.
for (let n = 0; n < 200; n++) {
  const z = heights([1.8, 1.8 + 0.01 * ((n * 7) % 30) - 0.1, 1.8 + 0.01 * ((n * 11) % 30) - 0.1, 1.8 + 0.01 * ((n * 13) % 30) - 0.1])
  const p = screwPlan(z)
  assert.equal(p.useOptimized, false, `standard stays standard (${n})`)
  assert.equal(p.shown[0], 0, `front left is never turned in standard (${n})`)
  assert.deepEqual(p.shown.map(um), measuredOffsets(z).map(um), `standard shows the measurement (${n})`)
}
// The choice also holds when the optimized plan would not help any more: it is only left by the explicit way back.
const mixed = heights([1.8, 1.85, 1.77, 1.81])
assert.equal(screwPlan(mixed).canOptimize, false)
assert.equal(screwPlan(mixed, 'optimized').useOptimized, false, 'not applicable: the shown plan is the standard one')
assert.equal(screwPlan(mixed, 'optimized').strategy, 'optimized', 'but the chosen strategy stays')
assert.equal(screwPlan(mixed, 'optimized').shown[0], 0, 'and front left is not turned')

// The offer needs the others off the same way by more than 20 µm and a gain of more than 5 µm (as before).
assert.equal(screwPlan(heights([1.8, 1.821, 1.827, 1.821])).canOptimize, true, 'gain 6 µm')
assert.equal(screwPlan(heights([1.8, 1.821, 1.826, 1.821])).canOptimize, false, 'gain exactly 5 µm')
assert.equal(screwPlan(heights([1.8, 1.82, 1.84, 1.82])).canOptimize, false, 'exactly 20 µm is no common shift')
// More than 0.5 mm of front-left move is never used, even when chosen.
const far = heights([1.8, 2.4, 2.41, 2.42])
assert.equal(screwPlan(far).canOptimize, false)
assert.equal(screwPlan(far, 'optimized').useOptimized, false)

assert.equal(parseScrewStrategy('optimized'), 'optimized')
for (const v of ['standard', '', null, undefined, 'x', 1]) assert.equal(parseScrewStrategy(v), 'standard', `${v} is standard`)
console.log('PASS screw strategy plan: standard by default, offer only, explicit choice, measured offsets apart')
