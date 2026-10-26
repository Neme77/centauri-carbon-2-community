export type PrintObject = {
  id: string
  label: string
  center: [number, number] | null
  polygon: [number, number][] | null
}

export const BED_SIZE = 256

export const onBed = (p: [number, number] | null): p is [number, number] =>
  p?.every(n => Number.isFinite(n) && n >= 0 && n <= BED_SIZE) === true

const point = (p: unknown): p is [number, number] =>
  Array.isArray(p) && p.length === 2 && p.every(n => typeof n === 'number' && Number.isFinite(n))

function decodeCoordinates(value: unknown): unknown {
  if (typeof value !== 'string') return value
  if (value.length > 1024 * 1024) return null
  try {
    return JSON.parse(value)
  } catch {
    return null
  }
}

function center(o: any): [number, number] | null {
  const middle = decodeCoordinates(o?.center)
  if (point(middle)) return [middle[0], middle[1]]
  const polygon = decodeCoordinates(o?.polygon)
  if (!Array.isArray(polygon) || polygon.length < 3 || !polygon.every(point)) return null
  let xmin = Infinity,
    xmax = -Infinity,
    ymin = Infinity,
    ymax = -Infinity
  for (const [x, y] of polygon) {
    xmin = Math.min(xmin, x)
    xmax = Math.max(xmax, x)
    ymin = Math.min(ymin, y)
    ymax = Math.max(ymax, y)
  }
  const x = xmin / 2 + xmax / 2,
    y = ymin / 2 + ymax / 2
  return Number.isFinite(x) && Number.isFinite(y) ? [x, y] : null
}

function outline(o: any): [number, number][] | null {
  const polygon = decodeCoordinates(o?.polygon)
  // Limit drawing work, and never project invalid coordinates onto an invented position.
  return Array.isArray(polygon) &&
    polygon.length >= 3 &&
    polygon.length <= 512 &&
    polygon.every(p => point(p) && onBed(p))
    ? polygon
    : null
}

function baseName(id: string): string {
  // Strip only known slicer instance suffixes. Never use this label as a command ID.
  return (
    id
      .normalize('NFC')
      .replace(/_id_\d+_copy_\d+$/i, '')
      .replace(/\.(stl|3mf|obj)$/i, '')
      .replace(/_(stl|3mf|obj)$/i, '')
      .replace(/_/g, ' ')
      .replace(/\s+/g, ' ')
      .trim() || id
  )
}

export function layoutObjects(raw: unknown[]): PrintObject[] {
  const rows = raw.flatMap((o: any) => {
    const id = typeof o === 'string' ? o : typeof o?.name === 'string' ? o.name : ''
    return id ? [{ id, label: baseName(id), center: center(o), polygon: outline(o) }] : []
  })
  const counts = new Map<string, number>()
  for (const o of rows) counts.set(o.id, (counts.get(o.id) || 0) + 1)
  // Ambiguous original IDs cannot safely identify a physical object for exclusion.
  const byY = rows
    .filter(o => counts.get(o.id) === 1)
    .sort((a, b) => {
      if (a.center && b.center)
        return a.center[1] - b.center[1] || a.center[0] - b.center[0] || a.id.localeCompare(b.id, 'en')
      if (a.center || b.center) return a.center ? -1 : 1
      return a.id.localeCompare(b.id, 'en', { numeric: true })
    })
  // A row spans at most 2 mm in center Y. Anchor at its first Y, rather than
  // chaining neighbours or using a non-transitive tolerance in a comparator.
  const groups: PrintObject[][] = []
  const unknown = byY.filter(o => !o.center)
  for (const o of byY) {
    if (!o.center) continue
    const row = groups[groups.length - 1]
    const first = row?.[0].center
    if (!row || !first || o.center[1] - first[1] > 2) groups.push([o])
    else row.push(o)
  }
  const sorted = groups
    .flatMap(row => row.sort((a, b) => (a.center?.[0] ?? 0) - (b.center?.[0] ?? 0) || a.id.localeCompare(b.id, 'en')))
    .concat(unknown)
  const totals = new Map<string, number>()
  for (const o of sorted) totals.set(o.label, (totals.get(o.label) || 0) + 1)
  const numbers = new Map<string, number>()
  const used = new Set<string>()
  // Reserve singleton labels so generated copy numbers cannot hide another model.
  for (const o of sorted) if (totals.get(o.label) === 1) used.add(o.label)
  return sorted.map(o => {
    if (totals.get(o.label) === 1) return o
    let n = (numbers.get(o.label) || 0) + 1
    while (used.has(`${o.label}-${n}`)) n++
    numbers.set(o.label, n)
    const label = `${o.label}-${n}`
    used.add(label)
    return { ...o, label }
  })
}
