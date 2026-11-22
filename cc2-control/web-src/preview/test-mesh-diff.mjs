// Pointwise change between two saved meshes: same grid subtracts, a different grid refuses.
import assert from 'node:assert/strict'
import { meshDelta } from '../src/lib/mesh.ts'

const grid = (z, bounds = {}) => ({
  min_x: 6,
  max_x: 246,
  min_y: 6,
  max_y: 246,
  ...bounds,
  points: [
    [z, z + 0.1, z + 0.2],
    [z + 0.3, z + 0.4, z + 0.5],
    [z + 0.6, z + 0.7, z + 0.8],
  ],
})

const delta = meshDelta(grid(0.2), grid(0.05))
assert.ok(delta)
assert.equal(delta.length, 9)
assert.equal(delta[0].x, 6)
assert.equal(delta[0].y, 6)
assert.equal(delta[8].x, 246)
assert.equal(delta[8].y, 246)
for (const point of delta) assert.ok(Math.abs(point.z - 0.15) < 1e-12)

assert.equal(meshDelta(grid(0.2, { max_x: 200 }), grid(0.05)), null)
assert.equal(meshDelta(grid(0.2, { max_x: 246.00005 }), grid(0.05)), null)
assert.equal(
  meshDelta(grid(0.2), { ...grid(0.05), points: grid(0.05).points.slice(0, 2) }),
  null
)
const shifted = grid(0.2)
shifted.points[1][1] = Number.NaN
assert.equal(meshDelta(shifted, grid(0.05)), null)

console.log('PASS: mesh change is the new height minus the previous one on the same grid')
