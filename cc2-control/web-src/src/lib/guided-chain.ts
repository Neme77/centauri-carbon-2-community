import { errText, notify } from './api'
import { t, tpl } from './i18n'
import { adoptWork } from './level-state'
import { plates } from './plates'
import { consoleStatus, printer, refreshConsole, refreshPrinter } from './state'
import { ss, store } from './store'

// "Measure a new mesh" (Bed alignment, step 1 with "With mesh" ticked): one mesh is measured as the plate's temporary
// mesh (CC2 Control keeps it in memory only, until it restarts or another mesh has been probed; the plate's stored
// measurement stays until the final mesh), then the way of measuring chosen in step 1 follows by itself: the quick
// measurement of the four screws or the start of a screw session. It is sent without a hold time (the bed is warm from
// the mesh). The chain is driven
// from the frame of the app (components/guided-chain), not from the Bed alignment tab, so it goes on while another page
// is open, and it is kept in this browser tab (sessionStorage), so it goes on after a reload. A new browser tab does not
// take it over: two tabs never start the same measurement twice.
//
// 'mesh'    the calibration runs; it has to end with the working mesh, for the remembered plate and temperature.
// 'screws'  the working mesh is measured and is the basis; the start of the promised screw measurement (or session) is
//           tried until the printer takes it, while it answers "busy" (every CHAIN_RETRY_MS, for at most
//           CHAIN_RETRY_LIMIT_MS).
// 'hint'    the chain ended without starting: a lasting note. 'work': the working mesh is there, the note has a button
//           that starts the way of measuring that was confirmed. 'ended': the calibration did not end as remembered, nothing to start.
//           'lost': CC2 Control no longer has the working mesh (restarted, another mesh probed, or the plate deleted),
//           nothing to start.
// Nothing is started when the chain was called off, a print runs, a screw session or another calibration runs, or a
// screw measurement was started meanwhile (by this page, another tab or another device): in doubt the note is shown.
type ChainStage = 'mesh' | 'screws' | 'hint'
export type Chain = {
  stage: ChainStage | null
  // What was confirmed: the plate that receives the mesh, the bed temperature, the hold time of the mesh (minutes) and
  // the way of measuring that follows with its bed target and probes, exactly as the action that is sent: the quick
  // measurement ("screws:measure:60:2") or the session start ("screws:tram:start:60:2").
  plate: string
  temp: number
  soak: number
  action: string
  // Date.now() of the calibration start; whether the calibration was seen running since.
  started: number
  seen: boolean
  // When CC2 Control measured the working mesh (its `measured`), and the console generation at that moment: a screw job
  // after it was not started by the chain.
  work: number
  gen: number
  firstTry: number
  nextTry: number
  tries: number
  // The last reason the printer gave for not taking the start (its own words), or the reason of the note.
  reason: string
  hint: '' | 'work' | 'ended' | 'lost'
}

// While the printer answers "busy" the start is tried again every 5 s. Five minutes cover what the printer may still do
// after the mesh (a command that finishes, a job of another client) with a wide margin; after that the user decides,
// with the note's button.
const CHAIN_RETRY_MS = 5000
const CHAIN_RETRY_LIMIT_MS = 5 * 60_000
// A calibration that is not seen running this long after its start is not the one of the chain.
const CHAIN_UNSEEN_MS = 30_000
// A chain older than its hold time plus this is not continued (a tab restored much later).
const CHAIN_MAX_AGE_MS = 60 * 60_000
const KEY = 'cc2-level-chain'
// A screw measurement (quick or of a session) in the console command.
export const SCREW_JOB = /CC2_SCREW_(MEASURE|TRAM)/
// What the chain may start after its mesh: the quick measurement or the session start, never with a hold time.
const chainAction = (a: unknown) =>
  typeof a === 'string' && (a.startsWith('screws:measure') || a.startsWith('screws:tram:start'))

const EMPTY: Chain = {
  stage: null,
  plate: '',
  temp: 0,
  soak: 0,
  action: '',
  started: 0,
  seen: false,
  work: 0,
  gen: 0,
  firstTry: 0,
  nextTry: 0,
  tries: 0,
  reason: '',
  hint: '',
}

const read = (): Chain => {
  try {
    const v = JSON.parse(ss.get(KEY) || 'null')
    if (!v || !['mesh', 'screws', 'hint'].includes(v.stage)) return EMPTY
    if (!chainAction(v.action) || typeof v.plate !== 'string') return EMPTY
    const num = (x: unknown) => (Number.isFinite(Number(x)) ? Number(x) : 0)
    return {
      ...EMPTY,
      stage: v.stage,
      plate: v.plate,
      temp: num(v.temp),
      soak: num(v.soak),
      action: v.action,
      started: num(v.started),
      seen: Boolean(v.seen),
      work: num(v.work),
      gen: num(v.gen),
      firstTry: num(v.firstTry),
      nextTry: num(v.nextTry),
      tries: num(v.tries),
      reason: typeof v.reason === 'string' ? v.reason : '',
      hint: v.hint === 'work' || v.hint === 'ended' || v.hint === 'lost' ? v.hint : '',
    }
  } catch {
    return EMPTY
  }
}

export const guidedChain = store<Chain>(read())
const save = (patch: Partial<Chain>) => {
  guidedChain.set(patch)
  if (guidedChain.get().stage) ss.set(KEY, JSON.stringify(guidedChain.get()))
  else ss.del(KEY)
}
// Also called off by the user: nothing is started; a running calibration goes on.
export const clearChain = () => save(EMPTY)
// When the chain told the stop of its mesh (see stepChain); pages that see the same end leave the message to it.
let cancelTold = 0
export const chainTellsCancel = () => guidedChain.get().stage === 'mesh' || Date.now() - cancelTold < 60_000
const note = (hint: 'work' | 'ended' | 'lost', reason = '') => save({ stage: 'hint', hint, reason })

// The start of a screw measurement, the same as the buttons of the tab use (pages/bed registers it): `quiet` throws a
// refusal back instead of telling it. True when the printer took it.
type Starter = (action: string, quiet: boolean) => Promise<boolean>
let starter: Starter | null = null
export const setScrewStarter = (fn: Starter) => {
  starter = fn
}
export const startScrews = (action: string, quiet: boolean) =>
  starter ? starter(action, quiet) : Promise.reject(Error('Screw measurement is not available'))

export const beginChain = (c: Pick<Chain, 'plate' | 'temp' | 'soak' | 'action'>) =>
  save({ ...EMPTY, ...c, stage: 'mesh', started: Date.now() })

// A refusal that goes away by itself: no answer, a server error, or the printer (or CC2 Control) still busy, also a
// mesh calibration that has not quite ended yet. A running screw session, a print, a screw measurement waiting for its
// hold time or anything else is not tried again.
const passingRefusal = (e: unknown) => {
  const status = Number((e as { status?: number })?.status) || 0
  const text = errText(e)
  if (!status || status >= 500) return true
  return (
    status === 409 &&
    /idle|still running|another command|busy|paused|retry|calibration is running/i.test(text) &&
    !/session|printing|hold time/i.test(text)
  )
}

const inUse = () => {
  const d = printer.get().data
  return Boolean(d?.screws_session?.active) || ['printing', 'paused'].includes(String(d?.print?.state || ''))
}

// The working mesh of the chain is still in CC2 Control: the same plate, temperature and time of measurement. A new
// calibration keeps it until its probing has ended (then the printer holds another mesh).
const workKept = (c: Chain) => {
  const w = plates.get().data?.calibration?.work_mesh
  return Boolean(w && w.plate === c.plate && w.temp === c.temp && w.measured === c.work)
}

// One step of the chain; the frame of the app runs it every second while a chain is kept (lib/poll, also in a hidden tab).
export async function stepChain() {
  const c = guidedChain.get()
  if (!c.stage) return
  const con = consoleStatus.get()
  const lib = plates.get()
  if (c.stage === 'hint') {
    // The note goes away once a screw measurement or a session runs.
    if ((con.busy && SCREW_JOB.test(con.command)) || printer.get().data?.screws_session?.active) clearChain()
    // A start button without the working mesh would evaluate with another basis: the note says so instead.
    else if (c.hint === 'work' && lib.ok && lib.data && !workKept(c)) note('lost')
    return
  }
  const cal = lib.data?.calibration
  // Only what was read after the start counts.
  if (!cal || lib.at <= c.started) return
  const ours = cal.plate === c.plate && cal.temp === c.temp
  if (c.stage === 'mesh') {
    // Stopped by the user (the calibration bar), also before the run was seen: the chain ends and tells it itself, on
    // every page (useBasis and the Mesh card leave it to the chain). Only an answer to a request sent after the start
    // counts, so the "cancelled" of an earlier run is never taken for this one.
    if (cal.stage === 'off' && cal.result === 'cancelled' && ours && lib.sent > c.started) {
      cancelTold = Date.now()
      clearChain()
      notify(t('bed.cal_cancelled'))
      return
    }
    if (cal.stage !== 'off') {
      if (!ours) note('ended')
      else if (!c.seen) save({ seen: true })
      return
    }
    if (!c.seen) {
      if (Date.now() - c.started > CHAIN_UNSEEN_MS) note('ended')
      return
    }
    const w = cal.work_mesh
    if (cal.result !== 'work' || !ours || !w || w.plate !== c.plate || w.temp !== c.temp) return note('ended')
    if (Date.now() - c.started > c.soak * 60_000 + CHAIN_MAX_AGE_MS) return note('ended')
    await refreshConsole()
    if (guidedChain.get().stage !== 'mesh') return
    save({
      stage: 'screws',
      work: w.measured,
      gen: consoleStatus.get().generation,
      firstTry: Date.now(),
      nextTry: Date.now(),
      tries: 0,
      reason: '',
    })
    // The working mesh is the basis of the evaluation, also when the tab is not open.
    await adoptWork().catch(() => false)
    return
  }
  if (Date.now() < c.nextTry) return
  await Promise.all([refreshConsole(), refreshPrinter()])
  const now = guidedChain.get()
  if (now.stage !== 'screws') return // called off meanwhile
  const job = consoleStatus.get()
  // A screw measurement was started since the working mesh (by hand, another tab, another device): nothing more to start.
  if (job.generation > now.gen && SCREW_JOB.test(job.command)) return clearChain()
  // CC2 Control no longer has the working mesh (restarted, another mesh probed, or the plate deleted): the screw points
  // would be evaluated with another basis, so they are not started.
  if (plates.get().ok && plates.get().data && !workKept(now)) return note('lost')
  if (inUse() || plates.get().data?.calibration.stage !== 'off') return note('work', t('bed.lvl_chain_in_use'))
  if (Date.now() - now.firstTry > CHAIN_RETRY_LIMIT_MS) return note('work', now.reason)
  save({ tries: now.tries + 1, nextTry: Date.now() + CHAIN_RETRY_MS })
  try {
    await startScrews(now.action, true)
    clearChain()
    notify(t('bed.lvl_chain_started'))
  } catch (e) {
    if (guidedChain.get().stage !== 'screws') return
    if (passingRefusal(e)) save({ reason: errText(e) })
    else note('work', errText(e))
  }
}

// The line of the chain in its bar (components/guided-chain). While the printer is busy the reason is kept, but not
// shown.
export const chainText = (c: Chain) =>
  c.stage === 'mesh'
    ? t('bed.lvl_chain_mesh')
    : c.stage === 'screws'
      ? c.reason
        ? tpl('bed.lvl_chain_waiting', { seconds: CHAIN_RETRY_MS / 1000 })
        : t('bed.lvl_chain_screws')
      : c.hint === 'work'
        ? tpl('bed.lvl_chain_hint', { reason: c.reason || '—' })
        : c.hint === 'lost'
          ? t('bed.lvl_chain_lost')
          : t('bed.lvl_chain_ended')
