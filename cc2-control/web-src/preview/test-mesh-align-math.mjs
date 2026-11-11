// Plane-fit analysis of the mesh-guided screw alignment, on synthetic meshes only: a plane with a known
// tilt plus a dome, a bowl, a saddle or noise. The tilt must come back and the residual must hold none.
import assert from 'node:assert/strict'
import {
  ALIGN_DEFAULTS,
  ALIGN_NOOP_MM,
  analyseMesh,
  analyseScrewProbes,
  fitPlane,
  gridFromMeshReply,
  gridPoints,
  meshAt,
  nameShape,
  parseAlignConfig,
  QUICK_TWIST_WARN_MM,
  referenceScrew,
  tiltsBelowTarget,
  latestScrewProbes,
  QUICK_PROBE_TEMP_C,
  QUICK_REFERENCE_MAX_AGE_DAYS,
  quickReference,
} from '../src/lib/mesh-align.ts'
import { SCREW_CORNERS } from '../src/lib/screws.ts'
import { asProfileReply, bowl, dome, noiseSource, saddle, syntheticMesh, twist } from './mesh-align-synthetic.mjs'

const near = (actual, expected, tolerance, message) =>
  assert.ok(Math.abs(actual - expected) <= tolerance, `${message}: ${actual} is not within ${tolerance} of ${expected}`)
const config = extra => ({ ...ALIGN_DEFAULTS, ...extra })
// What is left after the plane must have no slope of its own.
const untilted = (analysis, message) => {
  const plane = fitPlane(analysis.residual)
  near(plane.b * analysis.span, 0, 1e-9, `${message}: residual tilt X`)
  near(plane.c * analysis.span, 0, 1e-9, `${message}: residual tilt Y`)
  near(
    analysis.residual.reduce((s, p) => s + p.z, 0),
    0,
    1e-9,
    `${message}: residual mean`
  )
}

// The defaults are the CC2 standard and match the corners the 4-point test probes.
assert.deepEqual(
  ALIGN_DEFAULTS.screws,
  SCREW_CORNERS.map(c => ({ id: c.id, x: c.x, y: c.y }))
)
assert.equal(ALIGN_DEFAULTS.tilt_span_mm, 240)
assert.equal(ALIGN_DEFAULTS.target_tilt_mm, 0.1)
assert.equal(ALIGN_DEFAULTS.max_rounds, 4)
assert.equal(ALIGN_DEFAULTS.shape_warn_mm, 0.3)
assert.equal(ALIGN_DEFAULTS.row0_is_min_y, true)
assert.equal(ALIGN_DEFAULTS.thread_pitch_mm, null, 'no thread pitch is assumed')
assert.equal(ALIGN_DEFAULTS.clockwise_raises_bed, null, 'no turning direction is assumed')

// A plane alone: the tilt comes back, nothing is left over.
{
  const a = analyseMesh(syntheticMesh({ base: 0.2, tiltX: 0.31, tiltY: -0.72 }))
  near(a.tilt_x, 0.31, 1e-9, 'plane tilt X')
  near(a.tilt_y, -0.72, 1e-9, 'plane tilt Y')
  near(a.residual_range, 0, 1e-9, 'plane residual range')
  near(a.residual_rms, 0, 1e-9, 'plane residual rms')
  assert.equal(a.shape, 'flat')
  assert.equal(a.count, 121)
  assert.equal(a.aligned, false)
  assert.equal(a.shape_warning, false)
  near(a.raw_range, 0.31 + 0.72, 1e-9, 'the raw range still contains the tilt')
}

// A known tilt under each plate shape: the tilt comes back, the shape is named from the residual.
for (const [name, shape, expected] of [
  ['dome', dome(0.4), 'dome'],
  ['bowl', bowl(0.4), 'bowl'],
  ['saddle', saddle(0.4), 'saddle'],
  ['twist', twist(0.3), 'saddle'],
]) {
  for (const [tiltX, tiltY] of [
    [0, 0],
    [0.35, 0.21],
    [-0.12, 1.3],
    [-0.9, -0.03],
  ]) {
    const a = analyseMesh(syntheticMesh({ base: -0.1, tiltX, tiltY, shape }))
    near(a.tilt_x, tiltX, 1e-9, `${name} tilt X`)
    near(a.tilt_y, tiltY, 1e-9, `${name} tilt Y`)
    untilted(a, name)
    assert.equal(a.shape, expected, `${name} with tilt ${tiltX}/${tiltY} is named from the residual`)
    assert.ok(a.residual_range > 0.25 && a.residual_range < 0.85, `${name} residual range ${a.residual_range}`)
    assert.ok(a.residual_rms > 0 && a.residual_rms < a.residual_range)
  }
}

// The residual range of a dome does not depend on the tilt, the raw range does.
{
  const flat = analyseMesh(syntheticMesh({ shape: dome(0.4) }))
  const tilted = analyseMesh(syntheticMesh({ tiltX: 0.8, tiltY: -1.1, shape: dome(0.4) }))
  near(tilted.residual_range, flat.residual_range, 1e-9, 'dome residual range under tilt')
  near(tilted.residual_rms, flat.residual_rms, 1e-9, 'dome residual rms under tilt')
  assert.ok(tilted.raw_range > flat.raw_range + 0.5)
}

// Noise: the tilt comes back within the noise, the residual has no slope, and no curvature is named.
{
  const random = noiseSource(7)
  const a = analyseMesh(syntheticMesh({ tiltX: 0.25, tiltY: -0.4, shape: () => 0.15 * random() }))
  near(a.tilt_x, 0.25, 0.02, 'noisy tilt X')
  near(a.tilt_y, -0.4, 0.02, 'noisy tilt Y')
  untilted(a, 'noise')
  assert.equal(a.shape, 'shape', 'strong noise is an unnamed shape')
  const quiet = noiseSource(9)
  const b = analyseMesh(syntheticMesh({ tiltX: 0.25, tiltY: -0.4, shape: () => 0.01 * quiet() }))
  near(b.tilt_x, 0.25, 0.02, 'quiet tilt X')
  near(b.tilt_y, -0.4, 0.02, 'quiet tilt Y')
  assert.equal(b.shape, 'flat', 'noise below the flat range is nearly flat')
  // A dome under noise is still a dome, and its tilt still comes back.
  const mixed = noiseSource(11)
  const c = analyseMesh(syntheticMesh({ tiltX: -0.3, tiltY: 0.2, shape: (u, v) => dome(0.5)(u, v) + 0.02 * mixed() }))
  near(c.tilt_x, -0.3, 0.02, 'noisy dome tilt X')
  near(c.tilt_y, 0.2, 0.02, 'noisy dome tilt Y')
  assert.equal(c.shape, 'dome')
}

// Nothing depends on an 11 x 11 grid or on the CC2 mesh area.
{
  const a = analyseMesh(syntheticMesh({ cols: 7, rows: 9, min_x: 10, max_x: 240, min_y: 15, max_y: 235, tiltX: 0.2, tiltY: 0.6, shape: bowl(0.3) }))
  near(a.tilt_x, 0.2, 1e-9, '7 x 9 tilt X')
  near(a.tilt_y, 0.6, 1e-9, '7 x 9 tilt Y')
  assert.equal(a.count, 63)
  assert.equal(a.shape, 'bowl')
  untilted(a, '7 x 9')
  const three = analyseMesh(syntheticMesh({ cols: 3, rows: 3, tiltX: 0.2, tiltY: -0.1 }))
  near(three.tilt_x, 0.2, 1e-9, '3 x 3 tilt X')
  near(three.tilt_y, -0.1, 1e-9, '3 x 3 tilt Y')
  // The shape is judged over the measured area: a dome on a small mesh is a dome although its
  // corners are nowhere near the corners of the bed.
  const small = analyseMesh(syntheticMesh({ min_x: 80, max_x: 170, min_y: 80, max_y: 170, shape: dome(0.4) }))
  assert.equal(small.shape, 'dome')
  assert.equal(small.covers_screws, false, 'a mesh that ends inside the screws is an extrapolation')
  assert.equal(analyseMesh(syntheticMesh({})).covers_screws, true)
}

// Row order: row 0 is the front by default; the same rows read back to front flip tilt Y only.
{
  const grid = syntheticMesh({ tiltX: 0.3, tiltY: 0.5, shape: dome(0.2) })
  const front = analyseMesh(grid, config({ row0_is_min_y: true }))
  const back = analyseMesh(grid, config({ row0_is_min_y: false }))
  near(front.tilt_y, 0.5, 1e-9, 'row 0 at the front')
  near(back.tilt_y, -0.5, 1e-9, 'row 0 at the back')
  near(back.tilt_x, 0.3, 1e-9, 'tilt X does not depend on the row order')
  const stored = syntheticMesh({ tiltX: 0.3, tiltY: 0.5, row0IsMinY: false })
  near(analyseMesh(stored, config({ row0_is_min_y: false })).tilt_y, 0.5, 1e-9, 'a mesh stored back to front')
  assert.equal(gridPoints(grid)[0].y, 6)
  assert.equal(gridPoints(grid, false)[0].y, 246)
}

// The tilt span is a setting: the same plane over another distance.
near(analyseMesh(syntheticMesh({ tiltX: 0.24, span: 240 }), config({ tilt_span_mm: 100 })).tilt_x, 0.1, 1e-9, 'span')

// Screw corrections come from the plane at the screws, not from the tilt over the span: the rear screws
// sit 195 mm behind the front ones, so a Y tilt of t over 240 mm is t * 195 / 240 at the screws.
{
  const tiltY = 0.96
  const a = analyseMesh(syntheticMesh({ tiltY, shape: dome(0.4) }))
  const at = Object.fromEntries(a.screws.map(s => [s.id, s]))
  // The back is higher: with the front as reference the rear corners are lowered.
  assert.equal(a.reference, 'FL', 'all four tie on sum and maximum: the first screw is the reference')
  near(at.FL.correction, 0, 0, 'reference')
  near(at.FR.correction, 0, 1e-9, 'front right is level with front left')
  near(at.RR.correction, (-tiltY * 195) / 240, 1e-9, 'rear right')
  near(at.RL.correction, (-tiltY * 195) / 240, 1e-9, 'rear left')
  assert.ok(Math.abs(at.RL.correction) < Math.abs(a.tilt_y), 'the screw correction is smaller than the tilt')
  assert.deepEqual(
    a.screws.map(s => s.reference),
    [true, false, false, false]
  )
  // A lower corner gets a positive correction: raise the bed there.
  const b = analyseMesh(syntheticMesh({ tiltX: -0.6 }))
  const right = b.screws.find(s => s.id === 'FR'),
    left = b.screws.find(s => s.id === 'FL')
  assert.ok(right.correction - left.correction > 0, 'the lower right side is raised relative to the left')
  near(right.correction - left.correction, (0.6 * 200) / 240, 1e-9, 'X correction over the 200 mm between the screws')
}

// The reference is the screw with the smallest sum of corrections, then the smallest maximum, then the
// first one. No corner is fixed.
assert.equal(referenceScrew([0, 0, 0, 0]), 0)
assert.equal(referenceScrew([0, 0.1, 0.5, 0.2]), 3, 'sum ties between 1 and 3, the smaller maximum wins')
assert.equal(referenceScrew([0.5, 0.2, 0.1, 0]), 1, 'the same heights in another order')
assert.equal(referenceScrew([0.3, 0, 0, 0]), 1, 'one corner off: the first of the three level ones')
assert.equal(referenceScrew([0, 0, 0, 0.3]), 0)
assert.equal(referenceScrew([0.1, 0.4, 0.1, 0.4]), 0, 'a full tie keeps the first index')
for (const [tiltX, tiltY, expected] of [
  [0.5, 0.1, new Set(['FR', 'RL'])],
  [0.1, 0.5, new Set(['FR', 'RL'])],
  [-0.5, 0.1, new Set(['FL', 'RR'])],
]) {
  const a = analyseMesh(syntheticMesh({ tiltX, tiltY }))
  assert.ok(expected.has(a.reference), `tilt ${tiltX}/${tiltY}: reference ${a.reference} is a middle screw`)
  const sum = ref => a.screws.reduce((s, o) => s + Math.abs(ref.plane - o.plane), 0)
  const chosen = a.screws.find(s => s.reference)
  for (const other of a.screws) assert.ok(sum(chosen) <= sum(other) + 1e-12, 'no screw has a smaller sum')
  near(
    a.screws.reduce((s, o) => s + Math.abs(o.correction), 0),
    sum(chosen),
    1e-12,
    'corrections are relative to the reference'
  )
}

// Turns need a thread pitch and its source; the turning direction needs its own source.
{
  const grid = syntheticMesh({ tiltX: 0.48 })
  const none = analyseMesh(grid)
  assert.ok(none.screws.every(s => s.turns === null && s.direction === null), 'no source: millimetres only')
  const pitchOnly = analyseMesh(grid, config({ thread_pitch_mm: 0.5 }))
  assert.ok(pitchOnly.screws.every(s => s.turns === null), 'a pitch without a source gives no turns')
  const sourceOnly = analyseMesh(grid, config({ thread_pitch_source: 'measured' }))
  assert.ok(sourceOnly.screws.every(s => s.turns === null), 'a source without a pitch gives no turns')
  const turns = analyseMesh(grid, config({ thread_pitch_mm: 0.5, thread_pitch_source: 'measured' }))
  const moved = turns.screws.filter(s => !s.reference && Math.abs(s.correction) >= ALIGN_NOOP_MM)
  assert.ok(moved.length >= 2)
  for (const s of moved) near(s.turns, Math.abs(s.correction) / 0.5, 1e-12, 'turns from the pitch')
  assert.ok(turns.screws.every(s => s.direction === null), 'turns alone give no direction')
  const directionOnly = analyseMesh(grid, config({ clockwise_raises_bed: true }))
  assert.ok(directionOnly.screws.every(s => s.direction === null), 'a direction without a source is not shown')
  const blankSource = analyseMesh(grid, config({ clockwise_raises_bed: true, rotation_source: '   ' }))
  assert.ok(blankSource.screws.every(s => s.direction === null), 'a blank source is no source')
  for (const raises of [true, false]) {
    const a = analyseMesh(grid, config({ clockwise_raises_bed: raises, rotation_source: 'own test' }))
    for (const s of a.screws) {
      if (s.reference || Math.abs(s.correction) < ALIGN_NOOP_MM) assert.equal(s.direction, null)
      else assert.equal(s.direction, s.correction > 0 === raises ? 'cw' : 'ccw')
      assert.equal(s.turns, null, 'a direction alone gives no turns')
    }
  }
}

// "Aligned" needs both tilts below the target; the shape warning is independent of it.
{
  const target = ALIGN_DEFAULTS.target_tilt_mm
  const done = analyseMesh(syntheticMesh({ tiltX: 0.08, tiltY: -0.09, shape: dome(0.5) }))
  assert.equal(done.aligned, true)
  assert.equal(done.shape_warning, true, 'the warning stays although the alignment is done')
  assert.ok(done.residual_range > 0.3)
  assert.equal(analyseMesh(syntheticMesh({ tiltX: 0.04, tiltY: 0.12 })).aligned, false, 'one tilt above the target')
  assert.equal(analyseMesh(syntheticMesh({ tiltX: -0.13, tiltY: 0.01 })).aligned, false, 'the sign does not matter')
  assert.equal(tiltsBelowTarget(target, 0, target), false, 'at the target is not below it')
  assert.equal(tiltsBelowTarget(0, -target, target), false)
  assert.equal(tiltsBelowTarget(0.0999, -0.0999, target), true)
  assert.equal(analyseMesh(syntheticMesh({ tiltX: 0.06, tiltY: 0.06 }), config({ target_tilt_mm: 0.05 })).aligned, false)
  const smooth = analyseMesh(syntheticMesh({ tiltX: 0.9, shape: dome(0.1) }))
  assert.equal(smooth.shape_warning, false, 'a large tilt alone is no shape warning')
  assert.equal(analyseMesh(syntheticMesh({ shape: dome(0.2) }), config({ shape_warn_mm: 0.15 })).shape_warning, true)
}

// The shape name follows the residual and the configured flat range.
assert.equal(nameShape([], 0.01, 0.1), 'flat')
assert.equal(analyseMesh(syntheticMesh({ shape: dome(0.04) })).shape, 'flat')
assert.equal(analyseMesh(syntheticMesh({ shape: dome(0.04) }), config({ flat_range_mm: 0.02 })).shape, 'dome')
assert.equal(analyseMesh(syntheticMesh({ shape: (u, v) => -0.3 * u * u + 0 * v })).shape, 'dome', 'a bend along one axis')
assert.equal(analyseMesh(syntheticMesh({ shape: (u, v) => 0.3 * v * v + 0 * u })).shape, 'bowl')

// Too little to fit a plane.
assert.equal(analyseMesh({ points: [[0.1, 0.2]], min_x: 6, max_x: 246, min_y: 6, max_y: 246 }), null)
assert.equal(analyseMesh({ points: [], min_x: 6, max_x: 246, min_y: 6, max_y: 246 }), null)
assert.equal(fitPlane([{ x: 0, y: 0, z: 0 }, { x: 1, y: 1, z: 1 }, { x: 2, y: 2, z: 2 }]), null, 'points on one line')

// The mesh reply of GET /api/mesh: result.status.bed_mesh.profiles.<name>.points, or the active mesh.
{
  const grid = syntheticMesh({ tiltX: 0.2, tiltY: 0.1 })
  const reply = asProfileReply('default', grid)
  assert.deepEqual(gridFromMeshReply(reply, 'default'), grid)
  assert.equal(gridFromMeshReply(reply, 'default1'), null)
  assert.equal(gridFromMeshReply(reply, 'active'), null, 'an empty active mesh is no mesh')
  assert.equal(gridFromMeshReply({}, 'default'), null)
  const live = { result: { status: { bed_mesh: { mesh_min: [6, 6], mesh_max: [246, 246], probed_matrix: grid.points } } } }
  near(analyseMesh(gridFromMeshReply(live, 'active')).tilt_x, 0.2, 1e-9, 'active mesh')
}

// Settings stored in the browser: anything unusable falls back to the default.
{
  assert.deepEqual(parseAlignConfig(null), ALIGN_DEFAULTS)
  assert.deepEqual(parseAlignConfig('not json'), ALIGN_DEFAULTS)
  assert.notEqual(parseAlignConfig(null).screws, ALIGN_DEFAULTS.screws, 'the defaults are copied, not shared')
  const kept = parseAlignConfig(
    JSON.stringify({
      tilt_span_mm: 200,
      target_tilt_mm: 0.08,
      max_rounds: 6,
      shape_warn_mm: 0.4,
      thread_pitch_mm: 0.7,
      thread_pitch_source: 'measured with a caliper',
      clockwise_raises_bed: false,
      rotation_source: 'Elegoo wiki',
      row0_is_min_y: false,
      screws: [{ id: 'RR', x: 228, y: 226 }],
    })
  )
  assert.equal(kept.tilt_span_mm, 200)
  assert.equal(kept.target_tilt_mm, 0.08)
  assert.equal(kept.max_rounds, 6)
  assert.equal(kept.shape_warn_mm, 0.4)
  assert.equal(kept.thread_pitch_mm, 0.7)
  assert.equal(kept.clockwise_raises_bed, false)
  assert.equal(kept.row0_is_min_y, false)
  assert.deepEqual(kept.screws.find(s => s.id === 'RR'), { id: 'RR', x: 228, y: 226 })
  assert.deepEqual(kept.screws.find(s => s.id === 'FL'), { id: 'FL', x: 30, y: 30 })
  const bad = parseAlignConfig(
    JSON.stringify({ tilt_span_mm: -1, target_tilt_mm: 'x', max_rounds: 99, thread_pitch_mm: 0, clockwise_raises_bed: 'yes' })
  )
  assert.equal(bad.tilt_span_mm, 240)
  assert.equal(bad.target_tilt_mm, 0.1)
  assert.equal(bad.max_rounds, 4)
  assert.equal(bad.thread_pitch_mm, null)
  assert.equal(bad.clockwise_raises_bed, null)
}

// The quick mode: four probes at the screws, the plate shape taken from a reference mesh of the same plate.
{
  const reference = syntheticMesh({ tiltX: 0.02, tiltY: -0.03, shape: dome(0.4) })
  // The interpolation returns the mesh points themselves and nothing outside the measured area.
  near(meshAt(reference, reference.min_x, reference.min_y), reference.points[0][0], 1e-12, 'mesh at its first point')
  near(meshAt(reference, reference.max_x, reference.max_y), reference.points.at(-1).at(-1), 1e-12, 'mesh at its last point')
  near(
    meshAt(reference, reference.max_x, reference.max_y, false),
    reference.points[0].at(-1),
    1e-12,
    'rows read back to front'
  )
  assert.equal(meshAt(reference, reference.min_x - 1, 100), null)
  assert.equal(meshAt(reference, 100, reference.max_y + 1), null)
  const holed = { ...reference, points: reference.points.map((row, r) => row.map((z, c) => (r === 0 && c === 0 ? null : z))) }
  assert.equal(meshAt(holed, reference.min_x + 1, reference.min_y + 1), null)

  // The same plate after the screws were turned: the full mesh and the four probes agree.
  const turned = syntheticMesh({ tiltX: 0.31, tiltY: -0.22, shape: dome(0.4) })
  const full = analyseMesh(turned)
  const probes = Object.fromEntries(ALIGN_DEFAULTS.screws.map(s => [s.id, meshAt(turned, s.x, s.y)]))
  const quick = analyseScrewProbes(probes, reference)
  near(quick.tilt_x, full.tilt_x, 1e-9, 'quick tilt X')
  near(quick.tilt_y, full.tilt_y, 1e-9, 'quick tilt Y')
  assert.equal(quick.reference, full.reference)
  quick.screws.forEach((screw, i) => near(screw.correction, full.screws[i].correction, 1e-9, `quick correction ${screw.id}`))
  near(quick.twist, 0, 1e-9, 'a fitting reference leaves no twist')
  assert.equal(quick.twist_warning, false)
  assert.equal(quick.aligned, false)

  // Without the reference shape the same four probes would point at a tilt the plate does not have.
  const flat = syntheticMesh({ tiltX: 0, tiltY: 0 })
  const lopsided = syntheticMesh({ tiltX: 0, tiltY: 0, shape: (u, v) => 0.3 * u * u * u })
  const lopsidedProbes = Object.fromEntries(ALIGN_DEFAULTS.screws.map(s => [s.id, meshAt(lopsided, s.x, s.y)]))
  assert.ok(Math.abs(analyseScrewProbes(lopsidedProbes, flat).tilt_x) > 0.05)
  const own = analyseScrewProbes(lopsidedProbes, lopsided)
  near(own.tilt_x, analyseMesh(lopsided).tilt_x, 1e-9, 'own reference: tilt X')
  assert.equal(own.aligned, analyseMesh(lopsided).aligned)

  // A reference of another plate shows as a twist of the four points, and is warned about.
  const other = syntheticMesh({ tiltX: 0, tiltY: 0, shape: twist(0.2) })
  const wrong = analyseScrewProbes(probes, other)
  assert.ok(wrong.twist > QUICK_TWIST_WARN_MM)
  assert.equal(wrong.twist_warning, true)

  // Turns and direction follow the same gating as the full mode.
  const threaded = analyseScrewProbes(
    probes,
    reference,
    config({ thread_pitch_mm: 0.5, thread_pitch_source: 'measured', clockwise_raises_bed: false, rotation_source: 'wiki' })
  )
  const fullThreaded = analyseMesh(
    turned,
    config({ thread_pitch_mm: 0.5, thread_pitch_source: 'measured', clockwise_raises_bed: false, rotation_source: 'wiki' })
  )
  threaded.screws.forEach((screw, i) => {
    near(screw.turns, fullThreaded.screws[i].turns, 1e-9, `quick turns ${screw.id}`)
    assert.equal(screw.direction, fullThreaded.screws[i].direction)
  })
  assert.equal(quick.screws[0].turns, null)

  // Nothing is guessed: a missing probe, a screw outside the reference mesh or an unusable reference.
  assert.equal(analyseScrewProbes({ ...probes, RR: undefined }, reference), null)
  assert.equal(analyseScrewProbes({ ...probes, FL: Number.NaN }, reference), null)
  const small = syntheticMesh({ tiltX: 0, tiltY: 0 })
  assert.equal(analyseScrewProbes(probes, { ...small, min_x: 60 }), null)
  assert.equal(analyseScrewProbes(probes, { ...reference, points: [[0, 0]] }), null)
}

// Quick check: which full mesh may serve as the plate shape, and which four probes are used.
{
  const now = 2_000_000_000
  const mesh = syntheticMesh({ tiltX: 0, tiltY: 0 })
  const m = (id, temp, daysAgo) => ({ id, temp, measured: now - daysAgo * 86400, mesh })
  assert.deepEqual(quickReference(null, now), { ok: false, reason: 'no_plate' })
  assert.deepEqual(quickReference({ measures: [] }, now), { ok: false, reason: 'no_reference' })
  // A mesh at another bed temperature is not the plate shape of the four-point measurement.
  assert.deepEqual(quickReference({ measures: [m('hot', 80, 1)] }, now), { ok: false, reason: 'no_reference' })
  assert.equal(QUICK_PROBE_TEMP_C, 60)
  const fresh = quickReference({ measures: [m('older', 60, 9), m('newer', 60, 2), m('hot', 80, 0)] }, now)
  assert.equal(fresh.ok && fresh.measure.id, 'newer')
  assert.equal(fresh.ok && fresh.age_days, 2)
  assert.equal(quickReference({ measures: [m('edge', 60, QUICK_REFERENCE_MAX_AGE_DAYS)] }, now).ok, true)
  assert.deepEqual(quickReference({ measures: [m('old', 60, QUICK_REFERENCE_MAX_AGE_DAYS + 1)] }, now), {
    ok: false,
    reason: 'old',
    age_days: QUICK_REFERENCE_MAX_AGE_DAYS + 1,
  })

  const all = { FL: 0.1, FR: 0.2, RR: 0.3, RL: 0.4 }
  assert.equal(latestScrewProbes([]), null)
  // A single-screw probe is not a four-point measurement.
  assert.equal(latestScrewProbes([{ measurements: [{ time: 5, z: { FL: 0.1 } }] }]), null)
  const latest = latestScrewProbes([
    { measurements: [{ time: 10, z: all }, { time: 30, z: { RR: 0.9 } }] },
    { measurements: [{ time: 20, z: { ...all, RR: 0.5 } }] },
  ])
  assert.deepEqual(latest, { time: 20, z: { ...all, RR: 0.5 } })
}

console.log('PASS mesh-guided screw alignment: plane fit, tilt, residual, shape, reference screw, turns and direction gating, quick mode from four probes and a reference mesh')
