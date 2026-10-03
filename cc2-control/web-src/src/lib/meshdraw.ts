import type { Pt } from './mesh'

export type Cam = { yaw: number; pitch: number; zoom: number; scale: number }
export const defaultCam = (): Cam => ({ yaw: -0.28, pitch: 0.68, zoom: 1, scale: 1 })

const stops = [
  [24, 123, 213],
  [22, 199, 220],
  [103, 211, 176],
  [250, 203, 69],
]
const colour = (z: number, zMin: number, zMax: number, alpha = 1) => {
  const n = Math.max(0, Math.min(0.9999, zMax > zMin ? (z - zMin) / (zMax - zMin) : 0.5)) * 3,
    k = Math.floor(n),
    f = n - k,
    a = stops[k],
    b = stops[Math.min(3, k + 1)]
  return `rgba(${a.map((v, i) => Math.round(v + (b[i] - v) * f)).join(',')},${alpha})`
}

export const meshStats = (pts: Pt[]) => {
  if (pts.length < 4) return null
  const zs = pts.map(p => p.z),
    min = Math.min(...zs),
    max = Math.max(...zs)
  return { min, max, range: max - min, mean: zs.reduce((s, z) => s + z, 0) / zs.length }
}

// Renders the actual rectangular mesh as a 3D surface or a 2D heat map onto the canvas.
export type Labels = { empty: string; min: string; max: string; back: string }

export function drawMesh(canvas: HTMLCanvasElement, points: Pt[], mode: '3d' | '2d', cam: Cam, L: Labels) {
  const r = canvas.getBoundingClientRect()
  if (!r.width || !r.height) return
  const dpr = Math.min(devicePixelRatio || 1, 2)
  canvas.width = Math.round(r.width * dpr)
  canvas.height = Math.round(r.height * dpr)
  const c = canvas.getContext('2d'),
    w = r.width,
    h = r.height
  if (!c) return
  c.setTransform(dpr, 0, 0, dpr, 0, 0)
  c.clearRect(0, 0, w, h)
  c.font = '11px Inter, Segoe UI, sans-serif'
  const st = meshStats(points)
  if (!st) {
    c.fillStyle = '#8eafc2'
    c.textAlign = 'center'
    c.fillText(L.empty, w / 2, h / 2)
    return
  }
  const xs = [...new Set(points.map(p => p.x))].sort((a, b) => a - b)
  const ys = [...new Set(points.map(p => p.y))].sort((a, b) => a - b)
  const cells = new Map(points.map(p => [`${p.x},${p.y}`, p]))
  const { min: zMin, max: zMax } = st,
    col = (z: number, a = 1) => colour(z, zMin, zMax, a)
  if (mode === '2d') {
    const side = Math.min(w - 85, h - 70),
      left = (w - side) / 2,
      top = 22
    // Plot only the probed region on the bed, including non-square adaptive grids.
    for (const p of points) {
      const x = xs.indexOf(p.x),
        y = ys.indexOf(p.y)
      const x0 = x ? (xs[x - 1] + p.x) / 2 : p.x
      const x1 = x + 1 < xs.length ? (p.x + xs[x + 1]) / 2 : p.x
      const y0 = y ? (ys[y - 1] + p.y) / 2 : p.y
      const y1 = y + 1 < ys.length ? (p.y + ys[y + 1]) / 2 : p.y
      c.fillStyle = col(p.z)
      c.fillRect(
        left + (x0 / 250) * side,
        top + (1 - y1 / 250) * side,
        ((x1 - x0) / 250) * side + 0.2,
        ((y1 - y0) / 250) * side + 0.2
      )
    }
    c.fillStyle = '#a2c9dd'
    c.textAlign = 'center'
    c.fillText('X (mm) →', w / 2, h - 13)
    c.fillText(`${L.back} ↑`, w / 2, 13)
    return
  }
  const { yaw, pitch, zoom, scale } = cam
  const unit = Math.min(w / 365, h / 285) * zoom,
    origin = { x: w * 0.49, y: h * 0.59 }
  const project = (x: number, y: number, z: number) => {
    x -= 125
    y -= 125
    const xx = x * Math.cos(yaw) - y * Math.sin(yaw),
      yy = x * Math.sin(yaw) + y * Math.cos(yaw)
    return { x: origin.x + xx * unit, y: origin.y - yy * unit * Math.sin(pitch) - z * unit * 450 * scale }
  }
  const line = (a: any, b: any, color = '#245064', width = 1) => {
    c.strokeStyle = color
    c.lineWidth = width
    c.beginPath()
    c.moveTo(a.x, a.y)
    c.lineTo(b.x, b.y)
    c.stroke()
  }
  const floor = -0.18,
    ceiling = 0.24
  for (let i = 0; i <= 5; i++) {
    const t = i * 50
    line(project(t, 0, floor), project(t, 250, floor))
    line(project(0, t, floor), project(250, t, floor))
    line(project(t, 250, floor), project(t, 250, ceiling), '#1b3c4c')
    line(project(0, t, floor), project(0, t, ceiling), '#1b3c4c')
    c.fillStyle = '#a8cbdd'
    c.textAlign = 'center'
    let p = project(t, 0, floor)
    c.fillText(String(t), p.x, p.y + 18)
    p = project(250, t, floor)
    c.fillText(String(t), p.x + 20, p.y + 4)
  }
  for (let z = floor; z <= ceiling + 0.001; z += 0.105) {
    line(project(0, 0, z), project(0, 250, z), '#1f4557')
    line(project(0, 250, z), project(250, 250, z), '#1f4557')
    const p = project(0, 0, z)
    c.fillStyle = '#a8cbdd'
    c.textAlign = 'right'
    c.fillText(z.toFixed(2), p.x - 8, p.y + 4)
  }
  line(project(0, 0, floor), project(250, 0, floor), '#9fc2d4')
  line(project(250, 0, floor), project(250, 250, floor), '#9fc2d4')
  line(project(0, 0, floor), project(0, 0, ceiling), '#9fc2d4')
  const faces: { ps: Pt[]; depth: number }[] = []
  for (let y = 0; y + 1 < ys.length; y++)
    for (let x = 0; x + 1 < xs.length; x++) {
      const corners = [
        cells.get(`${xs[x]},${ys[y]}`),
        cells.get(`${xs[x + 1]},${ys[y]}`),
        cells.get(`${xs[x + 1]},${ys[y + 1]}`),
        cells.get(`${xs[x]},${ys[y + 1]}`),
      ]
      if (corners.some(p => !p)) continue
      const ps = corners as Pt[]
      faces.push({ ps, depth: ps.reduce((s, p) => s + p.x * Math.sin(yaw) + p.y * Math.cos(yaw), 0) / 4 })
    }
  faces.sort((a, b) => b.depth - a.depth)
  for (const { ps } of faces) {
    const qs = ps.map(p => project(p.x, p.y, p.z))
    c.beginPath()
    qs.forEach((q, i) => {
      if (i) c.lineTo(q.x, q.y)
      else c.moveTo(q.x, q.y)
    })
    c.closePath()
    c.fillStyle = col(ps.reduce((s, p) => s + p.z, 0) / 4, 0.94)
    c.fill()
    c.strokeStyle = '#d4f8ee55'
    c.lineWidth = 0.7
    c.stroke()
  }
  for (const p of points) {
    const q = project(p.x, p.y, p.z)
    c.fillStyle = '#d4fcff'
    c.beginPath()
    c.arc(q.x, q.y, 1.35, 0, Math.PI * 2)
    c.fill()
  }
  for (const [z, label, color] of [
    [zMin, L.min, '#16c9f2'],
    [zMax, L.max, '#ffcf4d'],
  ] as const) {
    const p = points.find(p => p.z === z)
    if (!p) continue
    const q = project(p.x, p.y, p.z)
    c.beginPath()
    c.arc(q.x, q.y, 5.5, 0, Math.PI * 2)
    c.fillStyle = color
    c.fill()
    c.strokeStyle = '#ffffff88'
    c.stroke()
    c.textAlign = 'left'
    c.font = 'bold 12px Inter, Segoe UI, sans-serif'
    c.fillText(label, q.x + 9, q.y - 17)
    c.fillStyle = '#e5f6fb'
    c.font = '11px Inter, Segoe UI, sans-serif'
    c.fillText(`${(z > 0 ? '+' : '') + z.toFixed(3)} mm`, q.x + 9, q.y - 3)
  }
  c.fillStyle = '#aad5e8'
  c.textAlign = 'center'
  c.font = '12px Inter, Segoe UI, sans-serif'
  let p = project(140, 0, floor)
  c.fillText('X (mm)', p.x, p.y + 40)
  p = project(250, 125, floor)
  c.fillText('Y (mm)', Math.min(w - 30, p.x + 52), p.y + 12)
  p = project(0, 0, ceiling)
  c.fillText('Z (mm)', p.x, p.y - 13)
}
