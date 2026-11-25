// The hold phase before a screw measurement (lib/screws withSoak): GET /api/printer reports it as screws_soak while the
// printer service heats the bed to the target and holds it there, before it runs the measurement or the session start.
// Active: {active, stage 'heating'|'soaking', bed (the target in °C), remaining, total (s; while heating remaining is
// total), action 'quick'|'corner'|'session'}. Not active: {active: false, result ''|'started'|'cancelled'|'ended'|'failed',
// reason}. The measured bed temperature is heater_bed, as everywhere.
export type ScrewSoak = {
  stage: 'heating' | 'soaking'
  // The bed target of the hold.
  bed: number | null
  // Seconds left of the hold time and the whole hold time.
  remaining: number
  total: number
  action: 'quick' | 'corner' | 'session'
}
const num = (v: unknown) =>
  v === null || v === undefined || v === '' || !Number.isFinite(Number(v)) ? null : Number(v)
// The hold phase while it is active, otherwise null.
export const screwSoakOf = (data: any): ScrewSoak | null => {
  const s = data?.screws_soak
  if (!s?.active) return null
  return {
    stage: s.stage === 'soaking' ? 'soaking' : 'heating',
    bed: num(s.bed),
    remaining: Math.max(0, num(s.remaining) ?? 0),
    total: Math.max(0, num(s.total) ?? 0),
    action: s.action === 'session' ? 'session' : s.action === 'corner' ? 'corner' : 'quick',
  }
}
// How a hold that is over ended: 'failed' or 'ended' with the service's reason are told once (components/guided-chain
// MeshStatus), 'cancelled' and 'started' say nothing more.
export const screwSoakEnd = (data: any): { result: string; reason: string } | null => {
  const s = data?.screws_soak
  if (!s || s.active) return null
  return { result: typeof s.result === 'string' ? s.result : '', reason: typeof s.reason === 'string' ? s.reason : '' }
}
