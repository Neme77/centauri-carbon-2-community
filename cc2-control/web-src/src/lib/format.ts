export const num = (v: any) => (Number.isFinite(Number(v)) ? Number(v).toFixed(1) : '—')
export const duration = (s: any) => {
  s = Number(s)
  return !Number.isFinite(s) || s < 0 ? '—' : `${Math.floor(s / 3600)}h ${Math.floor((s % 3600) / 60)}m`
}
export const finishTime = (s: any) => {
  s = Number(s)
  return !Number.isFinite(s) || s < 0 ? '—' : new Date(Date.now() + s * 1000).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })
}
export const fileSize = (b: any) => (Number(b || 0) < 1048576 ? `${(Number(b || 0) / 1024).toFixed(1)} KiB` : `${(Number(b || 0) / 1048576).toFixed(1)} MiB`)
export const signed = (v: number, digits = 3) => (v > 0 ? '+' : '') + v.toFixed(digits)
