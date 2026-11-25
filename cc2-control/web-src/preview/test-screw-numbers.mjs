// Screw page numbers: ties on a threshold, half micrometres, and what the console text may contain.
// Heights come from Klipper with six decimals, so the page must decide exact ties exactly and must not guess at odd text.
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { analyseMesh, gridFromMeshReply } from '../src/lib/mesh-align.ts'
import { cornerHeights, cornerReads, microns, probeResults, SCREW_KEYS, screwPlan, screwSamples } from '../src/lib/screws.ts'

const mm = um => (um / 1000).toFixed(6)
const heights = zs => Object.fromEntries(SCREW_KEYS.map((k, i) => [k, Number(zs[i])]))
const plan = (base, offsets) => screwPlan(heights([mm(base), ...offsets.map(o => mm(base + o))]))

const failed = []
const section = (n, fn) => {
  try {
    fn()
  } catch (e) {
    failed.push(`part ${n}: ${e.message.split('\n')[0]}`)
  }
}

section(1, () => {
  // 1. Exactly 20 um is "within tolerance" on every base height: the shown value must be exactly 0.02 (no noise above it).
  for (let base = -2000; base <= 2000; base++) {
    for (const dev of [20, -20]) {
      const p = screwPlan(heights([mm(base), mm(base + dev), mm(base), mm(base)]))
      assert.ok(Math.abs(p.shown[1]) <= 0.02, `${base} ${dev}: ${p.shown[1]} is more than 20 um`)
    }
  }
})
section(2, () => {
  // 2. The optimized plan is offered (canOptimize; it is only used after the operator's choice) when MORE than 20 um at every other corner and a gain of MORE than 5 um. Exact ties do not offer it.
  for (let base = -2000; base <= 2000; base++) {
    for (const s of [1, -1]) {
      assert.equal(plan(base, [20 * s, 40 * s, 20 * s]).canOptimize, false, `${base}: exactly 20 um`)
      assert.equal(plan(base, [21 * s, 26 * s, 21 * s]).canOptimize, false, `${base}: gain exactly 5 um`)
      assert.equal(plan(base, [21 * s, 27 * s, 21 * s]).canOptimize, true, `${base}: gain 6 um`)
      assert.equal(plan(base, [21 * s, 30 * s, 25 * s]).canOptimize, true, `${base}: 21 um and more`)
      assert.equal(plan(base, [21 * s, 30 * s, 25 * s]).useOptimized, false, `${base}: offered, never used by itself`)
    }
  }
})
section(3, () => {
  // 3. Half micrometres round away from zero for both signs, and a zero never carries a sign.
  for (let j = -4001; j <= 4001; j += 2) {
    const um = j / 2
    const want = `${um > 0 ? '+' : '−'}${Math.ceil(Math.abs(um))} µm`
    assert.equal(microns(Number((um / 1000).toFixed(6))), want, `half micrometre ${um}`)
  }
  assert.equal(microns(-0.0004), '0 µm')
  assert.equal(microns(0), '0 µm')
  assert.equal(microns(-0.0005), '−1 µm')
})
section(4, () => {
  // 4. Number text: an exponent is read as what it says; text that is not a plain number is not read at all.
  const probe = (z, n = 3) => `// probe at 27.900,25.950 is z=${z}\n`.repeat(n)
  assert.equal(screwSamples(probe('1.5e-02'))['27.9,25.95'], 0.015)
  assert.equal(screwSamples(probe('-0.700000'))['27.9,25.95'], -0.7)
  for (const bad of ['-0,700', '+0.100000', 'nan', 'inf', '1.5x', '--1']) assert.equal(screwSamples(probe(bad))['27.9,25.95'], undefined, bad)
})
section(5, () => {
  // 5. A result belongs to the probe point right before it, never to an earlier screw.
  const ok = '// probe at 27.900,25.950 is z=-0.700000\n// Result is z=-0.700000\n'
  assert.deepEqual(cornerHeights(`${ok}// probe at 128.000,128.000 is z=-0.100000\n// Result is z=-0.100000\n`), { '27.9,25.95': -0.7 })
  assert.deepEqual(cornerHeights(`${ok}// Result is z=-0.100000\n`), { '27.9,25.95': -0.7 })
  assert.equal(probeResults(`${ok}${ok.replace('-0.7', '-0.69').replace('-0.7', '-0.69')}`).length, 2)
})
section(6, () => {
  // 6. A mesh is a rectangle: a shorter row is refused, not stretched over the full width.
  const flat = (r, c, f) => Array.from({ length: r }, (_, i) => Array.from({ length: c }, (_, j) => f(j / (c - 1))))
  const reply = points => ({ result: { status: { bed_mesh: { probed_matrix: points, mesh_min: [15, 15], mesh_max: [245, 240] } } } })
  const tilted = flat(5, 5, u => u * 0.23)
  const g = gridFromMeshReply(reply(tilted))
  assert.deepEqual(analyseMesh(g).screws.map(s => Math.round(s.correction * 1000)), [0, -199, -199, 0]) // 1 µm per mm over the 199.25 mm between the screws
  const ragged = tilted.map((r, i) => (i === 2 ? r.slice(0, 3) : r))
  assert.equal(gridFromMeshReply(reply(ragged)), null)
})
// 7. The console reply as the printer sends it: every line is JSON, the value ends before the closing quote.
// Real reply of a screw session on a CC2 (4 corners, the homing probe at the bed centre first).
section(7, () => {
  const reply = JSON.parse(readFileSync(new URL('./fixtures/console-screw-session.json', import.meta.url), 'utf8'))
  assert.deepEqual(cornerHeights(reply.output), {
    '27.9,25.95': -0.715345,
    '227.15,25.95': -0.569811,
    '227.15,225.5': -0.77364,
    '27.9,225.5': -0.719039,
  })
  assert.equal(probeResults(reply.output).length, 4, 'the result of the centre probe belongs to no screw')
  // The average of the last three probes of a corner, as the printer's own Result is (seen in 127 of 127 results of one measurement run).
  assert.deepEqual(screwSamples(reply.output), {
    '27.9,25.95': -0.715344,
    '227.15,25.95': -0.569812,
    '227.15,225.5': -0.77364,
    '27.9,225.5': -0.719039,
  })
  // The single probes behind each Result are kept when their average is that Result (within 1 µm).
  const probes = Object.fromEntries(probeResults(reply.output).map(r => [r.key, r.samples.length]))
  assert.deepEqual(probes, { '27.9,25.95': 3, '227.15,25.95': 3, '227.15,225.5': 3, '27.9,225.5': 3 })
  assert.deepEqual(cornerReads(reply.output)['27.9,25.95'], { twice: false, probes: [-0.717333, -0.713227, -0.715473] })
  assert.deepEqual(cornerReads(`${'// probe at 27.900,25.950 is z=-0.5\n'.repeat(3)}// Result is z=-0.6\n`)['27.9,25.95'], { twice: false, probes: null }, 'probes that do not add up to the Result are dropped')
  const line = v => `{"id":0,"report":{"error_code":0,"message":"// probe at 27.900,25.950 is z=${v}"}}\n`
  assert.equal(screwSamples(line('-0,7').repeat(3))['27.9,25.95'], undefined, 'a comma value is still refused inside JSON')
})
if (failed.length) {
  console.error(failed.join('\n'))
  process.exit(1)
}
console.log('screw numbers: ok')
