export type Completion = { key: string; alias: string; path: string; seconds: number; live: boolean }
type Tracked = { path: string; uuid: string; run: number }
// Explicit completion evidence only; idle/100%, cancellation and lost connectivity
// cannot manufacture a successful print. Reuse the existing printer poll.
export class CompletionTracker {
  private running: Tracked | null = null
  private emitted = new Set<string>()
  private run = 0
  private readonly seen: Set<string>
  constructor(seen: Set<string>) {
    this.seen = seen
  }
  observe(d: any, ok: boolean): Completion | null {
    const age = Number(d?.last_message_age)
    if (!ok || !d?.connected || !Number.isFinite(age) || age < 0 || age > 15) return null
    const job = d?.print,
      native = job?.native
    const busy = Number(d?.machine?.status) === 2
    const state = String(native?.state || job?.state || '')
    if (busy && /^(printing|paused)$/.test(state)) {
      const path = String(native?.filename || job?.filename || '')
      const uuid = String(job?.uuid || '')
      if (path && (!this.running || this.running.path !== path || this.running.uuid !== uuid))
        this.running = { path, uuid, run: ++this.run }
      return null
    }
    if (/^(cancelled|error)$/.test(state)) {
      this.running = null
      return null
    }
    if (Number(d?.machine?.status) !== 1 || state !== 'complete') return null
    const path = String(native?.filename || job?.filename || this.running?.path || '')
    const seconds = Number(native?.duration ?? job?.duration)
    if (!path || !Number.isFinite(seconds) || seconds < 0 || (!this.running && seconds === 0)) return null
    if (this.running && this.running.path !== path) return null
    const uuid = this.running?.uuid || String(job?.uuid || '')
    const alias = `file:${path}:${seconds}`
    const live = Boolean(this.running)
    const key = uuid ? `uuid:${uuid}` : live ? `${alias}:run:${this.running?.run}` : alias
    this.running = null
    if (this.emitted.has(key) || this.seen.has(key) || (!live && this.seen.has(alias))) return null
    this.emitted.add(key)
    // Repeated snapshots may omit the UUID after MQTT clears the current job.
    this.emitted.add(alias)
    if (this.emitted.size > 64) this.emitted = new Set([...this.emitted].slice(-32))
    return { key, alias, path, seconds, live }
  }
}
export const replayable = (path: string) =>
  Boolean(path) &&
  !path.startsWith('/') &&
  !path.includes('\\') &&
  !path.split('/').some(p => !p || p === '.' || p === '..') &&
  /\.(gcode|gco|g)$/i.test(path)
