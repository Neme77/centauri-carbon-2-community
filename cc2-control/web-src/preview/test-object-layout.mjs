import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { layoutObjects } from '../src/lib/object-layout.ts'
const raw = [
  { name: 'Buddha.stl_id_3_copy_0', center: [150, 150] },
  { name: 'Buddha.stl_id_0_copy_0', center: [150, 50] },
  { name: 'Buddha.stl_id_2_copy_0', polygon: [[40, 140], [60, 140], [60, 160], [40, 160]] },
  { name: 'Buddha.stl_id_1_copy_0', center: [50, 50] },
]
const rows = layoutObjects(raw)
const captured = JSON.parse(await readFile(new URL('./fixtures/elegoo-object-coordinates.json',import.meta.url)))
const actual = layoutObjects(captured.result.status.exclude_object.objects)
assert.deepEqual(actual.map(o=>o.id), ['GPUSUPPORT_SCREW.STL_ID_1_COPY_0','GPUSUPPORT_STOP.STL_ID_2_COPY_0','GPUSUPPORT_BASE.STL_ID_0_COPY_0'])
assert.deepEqual(actual.map(o=>o.center), [[47.409,115.154],[116.549,113.552],[185.813,114.082]])
assert.deepEqual(layoutObjects([{name:'P',polygon:'[[0,0],[2,0],[2,2]]'}])[0].center,[1,1])
for (const value of ['not json','[null,1]','["2",3]','[1e999,3]','"[1,2]"']) assert.equal(layoutObjects([{name:'P',center:value}])[0].center,null)
const anchored = layoutObjects([{name:'A',center:[50,0]},{name:'B',center:[10,1.5]},{name:'C',center:[0,3]}])
assert.deepEqual(anchored.map(o=>o.id),['B','A','C'],'row tolerance must not chain or break comparator transitivity')
assert.deepEqual(rows.map(o => [o.id, o.label, o.center]), [
  [raw[3].name, 'Buddha-1', [50, 50]], [raw[1].name, 'Buddha-2', [150, 50]],
  [raw[2].name, 'Buddha-3', [50, 150]], [raw[0].name, 'Buddha-4', [150, 150]],
])
assert.deepEqual(layoutObjects([...raw].reverse()), rows, 'input order does not renumber objects')
const missing = layoutObjects([{ name: 'A', center: [null, 2] }, { name: 'B', center: ['1', 2] }, { name: 'C', center: [1, 2] }])
assert.deepEqual(missing.map(o => o.id), ['C', 'A', 'B'])
assert.equal(missing[1].center, null)
assert.deepEqual(layoutObjects(['duplicate', 'duplicate']), [], 'duplicate command IDs are ambiguous')
assert.equal(layoutObjects([{ name: 'Buddha-7', center: [0, 0] }])[0].label, 'Buddha-7', 'meaningful numeric names are preserved')
assert.equal(new Set(layoutObjects([...raw, 'Buddha-1']).map(o => o.label)).size, 5, 'copy labels cannot collide with another model')
const original = 'pièce_测试_"x".stl_id_0_copy_0'
assert.equal(layoutObjects([{name: original}])[0].id, original, 'original ID is byte-for-byte unchanged')
const many = Array.from({length:40}, (_, i) => ({ name:`Buddha_id_${i}_copy_0`, center:[(i % 8)*25, Math.floor(i/8)*25] }))
assert.deepEqual(layoutObjects(many.reverse()).map(o => o.label), Array.from({length:40}, (_,i) => `Buddha-${i+1}`))
console.log('PASS: spatial order, stable copy numbering, polygon fallback, missing coordinates, collisions and original IDs')
