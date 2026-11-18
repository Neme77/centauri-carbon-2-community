const round2 = (n: number) => Math.round(n * 100) / 100

function oval(cx: number, cy: number, rx: number, ry: number, n = 32): [number, number][] {
  return Array.from({ length: n + 1 }, (_, i) => {
    const a = (i / n) * Math.PI * 2
    return [round2(cx + rx * Math.cos(a)), round2(cy + ry * Math.sin(a))]
  })
}

function diagonalInfill(cx: number, cy: number, rx: number, ry: number, step = 4.2): [number, number][][] {
  const irx = rx * 0.7
  const iry = ry * 0.7
  const inside = (x: number, y: number) => ((x - cx) / irx) ** 2 + ((y - cy) / iry) ** 2 <= 1
  const paths: [number, number][][] = []
  for (let offset = -irx * 1.7; offset <= irx * 1.7; offset += step) {
    const pts: [number, number][] = []
    for (let s = -1; s <= 1.001; s += 0.045) {
      const x = cx + offset + s * irx
      const y = cy + s * iry
      if (inside(x, y)) pts.push([round2(x), round2(y)])
    }
    if (pts.length >= 2) paths.push(pts)
  }
  return paths
}

function island(cx: number, cy: number, rx: number, ry: number, infill: boolean): [number, number][][] {
  const paths = [oval(cx, cy, rx, ry), oval(cx, cy, rx * 0.78, ry * 0.78)]
  if (infill) paths.push(...diagonalInfill(cx, cy, rx, ry))
  return paths
}

export function demoLayerPath(file: string, layer: number, storage = 'internal') {
  // Consecutive layers differ by ~2.4 mm so the previous layer reads as a shadow.
  const grow = layer % 2 === 0 ? 2.4 : 0
  const buddha = island(128, 120, 30 + grow, 24 + grow, true)
  const copy2 = island(70, 192, 20 + grow * 0.7, 17 + grow * 0.7, true)
  const copy3 = island(190, 190, 18 + grow * 0.7, 16 + grow * 0.7, true)
  const paths = [...buddha, ...copy2, ...copy3]
  if (layer <= 8) paths.push(...island(190, 48, 16, 11, false))
  return {
    storage,
    file,
    layer,
    total_layers: 227,
    z: Math.round(layer * 0.2 * 100) / 100,
    paths,
    point_count: paths.reduce((n, p) => n + p.length, 0),
    truncated: false,
  }
}

export function demoExcludeObjects() {
  return {
    result: {
      status: {
        exclude_object: {
          objects: [
            { name: 'Buddha', center: [128, 120], polygon: oval(128, 120, 30, 24, 20).slice(0, -1) },
            { name: 'Copy 2', center: [70, 192], polygon: oval(70, 192, 20, 17, 18).slice(0, -1) },
            { name: 'Copy 3', center: [190, 190], polygon: oval(190, 190, 18, 16, 18).slice(0, -1) },
            { name: 'Brim tab', center: [190, 48], polygon: oval(190, 48, 16, 11, 16).slice(0, -1) },
          ],
          excluded_objects: ['Brim tab'],
          current_object: 'Buddha',
        },
      },
    },
  }
}

// The demo objects for the object preview: centre, footprint radii, object layers, and how the radius changes with
// the height (0 at the bed, 1 at the top). The answer follows the service: every step-th layer, at most 24 of them.
const demoShapes: Record<
  string,
  { c: [number, number]; r: [number, number]; layers: number; profile: (h: number) => number }
> = {
  // A seated figure: wide base, belly, shoulders, head.
  Buddha: {
    c: [128, 120],
    r: [30, 24],
    layers: 227,
    profile: h =>
      h < 0.62
        ? 1 - 0.55 * h ** 1.6
        : h < 0.7
          ? 0.42
          : 0.45 * Math.sqrt(Math.max(0, 1 - ((h - 0.84) / 0.16) ** 2)) + 0.02,
  },
  'Copy 2': { c: [70, 192], r: [20, 17], layers: 151, profile: h => 1 - 0.6 * h },
  'Copy 3': { c: [190, 190], r: [18, 16], layers: 60, profile: () => 1 },
  'Brim tab': { c: [190, 48], r: [16, 11], layers: 8, profile: () => 1 },
}

export function demoObjectPreview(file: string, object: string, storage = 'internal') {
  const shape = demoShapes[object]
  if (!shape) return null
  const step = Math.max(3, Math.ceil(shape.layers / 24))
  const rings = []
  for (let layer = 0; layer < shape.layers; layer += step) {
    const scale = Math.max(0.03, shape.profile(shape.layers > 1 ? layer / (shape.layers - 1) : 0))
    rings.push({
      z: round2((layer + 1) * 0.2),
      paths: [oval(shape.c[0], shape.c[1], shape.r[0] * scale, shape.r[1] * scale, 40).flat()],
    })
  }
  return {
    storage,
    file,
    object,
    layers: shape.layers,
    step,
    z_max: round2(shape.layers * 0.2),
    bounds: [shape.c[0] - shape.r[0], shape.c[1] - shape.r[1], shape.c[0] + shape.r[0], shape.c[1] + shape.r[1]],
    rings,
    truncated: false,
    objects: Object.keys(demoShapes),
  }
}
