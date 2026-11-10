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
