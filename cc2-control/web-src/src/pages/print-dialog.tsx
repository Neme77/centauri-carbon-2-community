import { useState } from 'preact/hooks'
import { ListOrdered, Minus, Play, Plus } from 'lucide-preact'
import { store } from '@/lib/store'
import { addToQueue, refreshQueue, type ToolFilament } from '@/lib/queue'
import { Button } from '@/components/ui/button'
import { Dialog, DialogTitle } from '@/components/ui/dialog'
import {
  FeedChoice,
  isSlot,
  isStock,
  slotColour,
  slotEmptyNote,
  slotLabel,
  stockBrands,
  stockOf,
} from '@/components/feed-choice'
import { Select } from '@/components/ui/field'
import { errText, notify, post, request, toast } from '@/lib/api'
import { ask } from '@/lib/confirm'
import { largeFileWarning } from '@/lib/large-file-warning'
import { LARGE_OK, tooLargeForPrinter } from '@/lib/printer-file-limit'
import { t, tpl } from '@/lib/i18n'
import { canvasModel } from '@/lib/canvas'
import { meshRoot } from '@/lib/mesh'
import { poll } from '@/lib/poll'
import { nearestMeasure, nozzleName, type PlateLibrary, usable } from '@/lib/plates'
import { openPage, printer } from '@/lib/state'
import { EXTERNAL, findSpool, g, spoolGrams, spoolLabel, type SpoolLibrary } from '@/lib/spools'
import { startTimeoutMs } from '@/lib/start-timeout'

type Pending = {
  storage: string
  path: string
  size: number | null // bytes, for how long the start may take
  tools: number[]
  // mm: the slicer's filament length for the tool, when the file states it.
  filaments: { tool: number; color: string; material: string; mm?: number }[]
  meshAvailable: MeshAvailability
  meshError: string | null
  trays: any[]
  connected: boolean
  preferExternal: boolean
  spools: SpoolLibrary | null
  // The build-plate library, and the file's first-layer bed temperature and nozzle diameter when it states them.
  plates: PlateLibrary | null
  bedTemp: number | null
  nozzleDiameter: number | null
  // The operator confirmed a file of 2 GiB or more: its start and its queued job carry "large_ok".
  largeOk: boolean
}
type MeshAvailability = { A: boolean | null; B: boolean | null }
type MeshReadiness = { availability: MeshAvailability; error: string | null }

// A failed or malformed mesh read is UNKNOWN, never evidence that no mesh exists.
// This is a read-only request; retrying must not start calibration or a print.
export async function readMeshReadiness(): Promise<MeshReadiness> {
  try {
    const data = await request('/api/mesh')
    const profiles = meshRoot(data)?.profiles
    if (!profiles || typeof profiles !== 'object' || Array.isArray(profiles))
      return { availability: { A: null, B: null }, error: t('state.unknown') }
    const readSide = (name: string): boolean | null => {
      if (!Object.keys(profiles).includes(name)) return false
      const profile = profiles[name]
      return profile !== null && typeof profile === 'object' && !Array.isArray(profile) ? true : null
    }
    const availability = { A: readSide('default'), B: readSide('default1') }
    return { availability, error: availability.A === null || availability.B === null ? t('state.unknown') : null }
  } catch (error) {
    return {
      availability: { A: null, B: null },
      error: errText(error),
    }
  }
}

const pending = store({ job: null as Pending | null })
let activeGeneration = 0,
  lastSeen = 0,
  opening = false

async function clearOrcaPending(generation: number) {
  if (!generation) return
  try {
    await post('/api/orca/pending-print/clear', String(generation))
  } catch (e) {
    notify(tpl('print.cannot_clear_orcaslicer_request', { error: errText(e) }), 'error')
  }
}

let fileAnalysisPending = false

// Opens the confirmation dialog; a print is never started without the operator confirming the mapping.
export async function startFile(storage: string, path: string) {
  return startFileOfSize(storage, path, null)
}
// The same with the file's size, so the page waits as long as the start of that file needs (start-timeout.ts).
export async function startFileOfSize(storage: string, path: string, size: number | null) {
  if (pending.get().job || fileAnalysisPending) return
  // From 2 GiB on the printer program has not opened such a file: warn before anything is read or copied (with the
  // estimated copy time and space); the operator may still go on, and the start then carries the confirmation.
  const largeOk = tooLargeForPrinter(size)
  if (largeOk && !(await ask(largeFileWarning(storage, size), true))) return
  fileAnalysisPending = true
  notify(t('print.analysing_file'))
  try {
    const [inspection, canvasData, meshReadiness, library, plateLibrary, feed] = await Promise.all([
      post('/api/gcode-files/inspect', `${storage}\n${path}`),
      request('/api/canvas').catch(() => ({ available: false })),
      readMeshReadiness(),
      printer.get().data?.spools?.enabled ? request('/api/spools').catch(() => null) : null,
      request('/api/plates').catch(() => null),
      request('/api/external-filament').catch(() => null),
    ])
    const tools: number[] = Array.isArray(inspection.tools) && inspection.tools.length ? inspection.tools : [0]
    const model = canvasModel(canvasData)
    pending.set({
      job: {
        storage,
        path,
        size: typeof size === 'number' ? size : null,
        tools,
        filaments: Array.isArray(inspection.filaments) ? inspection.filaments : [],
        meshAvailable: meshReadiness.availability,
        meshError: meshReadiness.error,
        trays: model?.trays || [],
        connected: Boolean(model?.connected),
        preferExternal: feed?.experiment === true && feed?.source === 'external',
        spools: library?.available && library.enabled ? library : null,
        plates: plateLibrary?.available ? plateLibrary : null,
        bedTemp: typeof inspection.bed_temperature === 'number' ? inspection.bed_temperature : null,
        nozzleDiameter: typeof inspection.nozzle_diameter === 'number' ? inspection.nozzle_diameter : null,
        largeOk,
      },
    })
    toast.set(s => ({ ...s, text: '' }))
  } catch (e) {
    pending.set({ job: null })
    // A start of this same file is still reading it: the printer does not analyse it in parallel.
    const reason = (e as { data?: { reason?: string } })?.data?.reason
    const m =
      reason === 'start_reading'
        ? t('print.start_reading')
        : tpl('print.cannot_prepare_print_error', { error: errText(e) })
    notify(m, 'error')
  } finally {
    fileAnalysisPending = false
  }
}

// An OrcaSlicer "Upload and Print" queues a confirmation here, not an unattended print.
export async function checkOrcaPendingPrint() {
  if (opening || pending.get().job) return
  opening = true // lock before the request so overlapping polls cannot open two dialogs
  try {
    const job = await request('/api/orca/pending-print')
    if (!job?.pending || !job.filename || !job.generation || job.generation === lastSeen) return
    lastSeen = job.generation // consume this automatic attempt even when preparation fails
    await startFile('internal', job.filename)
    if (pending.get().job?.path === job.filename) {
      activeGeneration = lastSeen = job.generation
      openPage('files')
      notify(t('print.orcaslicer_upload_complete_choose'))
    }
  } catch {
    /* keep the request pending for a retry */
  } finally {
    opening = false
  }
}

const reasonOf = (err: unknown) => (err as { data?: { reason?: string } })?.data?.reason

// A confirmed start of a file of 2 GiB or more: CC2 Control watches whether the printer begins it (status
// print_watch, see printer-file-limit.ts). Resolves with the outcome once it is decided; the backend decides within
// 30 minutes at the latest, so after a little longer the page stops waiting ("unknown").
const WATCH_WAIT_MAX_MS = 1_860_000
const watchOutcome = (sequence: number) =>
  new Promise<{ state: string; code: number }>(resolve => {
    const until = Date.now() + WATCH_WAIT_MAX_MS
    const check = () => {
      const w = printer.get().data?.print_watch
      const decided = w && Number(w.sequence) === sequence && w.state !== 'watching'
      if (!decided && Date.now() < until) return
      stop()
      resolve(decided ? { state: String(w.state), code: Number(w.code) || 0 } : { state: 'unknown', code: 0 })
    }
    const stop = poll(check, 500)
  })

// A USB read that stopped answering (reason usb_unresponsive, status usb_stall): while it hangs the operator removes
// the stick and cannot start again; once it is free again the reason it ended is shown with the advice.
const stallNote = (s: any) =>
  s.hanging
    ? t(s.power_cycle ? 'print.usb_stall_power_cycle' : 'print.usb_stall_hanging')
    : `${t('print.usb_stall_ready')} ${t(s.outcome === 'read_error' ? 'print.usb_stall_read_error' : 'print.usb_stall_slow')}`

export const PrintDialog = () => {
  const job = pending.use().job
  return job ? <Form job={job} /> : null
}

const Form = ({ job }: { job: Pending }) => {
  const multi = job.tools.length > 1
  const [useCanvas, setUse] = useState(job.connected && (!job.preferExternal || multi))
  const [map, setMap] = useState<Record<number, string>>({})
  const [side, setSide] = useState<'A' | 'B'>('A')
  const [calibrate, setCalibrate] = useState(false)
  const [timelapse, setTimelapse] = useState(false)
  // Start now, or keep the job with these choices in the print queue (copies are separate prints).
  const [launch, setLaunch] = useState<'now' | 'queue'>('now')
  const [copies, setCopies] = useState(1)
  const [position, setPosition] = useState<'end' | 'next'>('end')
  const [busy, setBusy] = useState(false)
  const [meshAvailable, setMeshAvailable] = useState(job.meshAvailable)
  const [meshError, setMeshError] = useState(job.meshError)
  const [meshChecking, setMeshChecking] = useState(false)
  const [largeOk, setLargeOk] = useState(job.largeOk)
  // The stall a start ran into (its sequence in the status), shown until the next start.
  const [stall, setStall] = useState<number | null>(null)
  const liveStall = printer.use().data?.usb_stall
  const stalled = stall !== null && liveStall && Number(liveStall.sequence) === stall ? liveStall : null
  const stallHangs = Boolean(stalled?.hanging)
  // A file whose size the page did not know (the server refused it as 2 GiB or more): ask now, then try again.
  const confirmLarge = async (err: unknown) => {
    if (reasonOf(err) !== 'file_too_large_for_printer' || largeOk) return false
    if (!(await ask(largeFileWarning(job.storage, job.size), true))) return false
    setLargeOk(true)
    return true
  }
  const [note, setNote] = useState(
    t(
      job.connected
        ? 'print.choose_the_spool_and_confirm_the'
        : multi
          ? 'print.canvas_not_detected_a_multicolour'
          : 'print.canvas_not_detected_external'
    )
  )
  const tray = (i: number) => job.trays.find(x => x && Number(x.tray_id) === i)
  // A queued job may wait for a filament that is in no slot: the spool inventory names the ones there are.
  const stock = stockBrands(job.spools)
  const available = meshAvailable[side],
    unknown = available === null,
    forced = available === false,
    calibrating = forced || calibrate
  // The mounted plate's measurement for a print on its saved mesh (nearest to the file's bed temperature unless
  // chosen) and the nozzle on the printer (the installed one, or the only one matching the file's diameter).
  const [measure, setMeasure] = useState<string | null>(null)
  const [nozzle, setNozzle] = useState<string | null>(null)
  const lib = job.plates
  const mounted = lib?.plates.find(p => p.id === lib.current)
  const plateHere = mounted?.side === side ? mounted : undefined
  const sameSize = (diameter: number) => job.nozzleDiameter !== null && Math.abs(diameter - job.nozzleDiameter) < 0.001
  const installed = lib?.nozzles.find(n => n.id === lib.nozzle)
  const matching = lib?.nozzles.filter(n => sameSize(n.diameter)) || []
  const nozzleId =
    nozzle ??
    (job.nozzleDiameter === null || (installed && sameSize(installed.diameter)) || matching.length !== 1
      ? lib?.nozzle || ''
      : matching[0].id)
  const chosenNozzle = lib?.nozzles.find(n => n.id === nozzleId)
  const nearest = plateHere ? nearestMeasure(plateHere, job.bedTemp, nozzleId) : null
  const measureId =
    plateHere && !calibrating
      ? measure !== null && plateHere.measures.some(m => m.id === measure && usable(m))
        ? measure
        : nearest?.id || ''
      : ''
  // Spool tracking: the spool each tool would draw from, and whether it holds what the slicer expects.
  const supply = (tool: number) => {
    const lib = job.spools
    const slot = useCanvas ? (isSlot(map[tool]) ? Number(map[tool]) : -1) : EXTERNAL
    if (!lib || slot < 0) return null
    const spool = findSpool(lib, lib.slots[slot]?.spool || '')
    const mm = Number(job.filaments.find(f => f.tool === tool)?.mm)
    return { spool, need: spool && mm > 0 ? spoolGrams(spool, mm) : null }
  }
  const short = job.tools.some(tool => {
    const s = supply(tool)
    return s?.spool && s.need !== null && s.need > s.spool.remaining
  })

  const recheckMesh = async () => {
    if (busy || meshChecking) return
    setMeshChecking(true)
    try {
      const next = await readMeshReadiness()
      setMeshAvailable(next.availability)
      setMeshError(next.error)
    } finally {
      setMeshChecking(false)
    }
  }

  const close = () => {
    if (busy) return
    const g = activeGeneration
    activeGeneration = 0
    pending.set({ job: null })
    void clearOrcaPending(g)
  }
  const submit = async (e: Event) => {
    e.preventDefault()
    if (busy || meshChecking) return
    if (launch === 'now' && unknown && !calibrating)
      return setNote(tpl('bed.mesh_unavailable_error', { error: meshError || t('state.unknown') }))
    // Every tool needs a slot; a queued job may name a filament of the inventory instead and wait for it.
    const free: ToolFilament[] =
      useCanvas && launch === 'queue'
        ? job.tools.flatMap(tool => {
            const s = stockOf(stock, map[tool])
            return s ? [{ tool, type: s.type, name: s.name, color: s.color }] : []
          })
        : []
    const slotted = job.tools.filter(tool => isSlot(map[tool]))
    if (useCanvas && launch === 'queue' && job.tools.some(tool => isStock(map[tool]) && !stockOf(stock, map[tool])))
      return setNote(t('queue.filament_stock_choose'))
    if (useCanvas && slotted.length + free.length < job.tools.length)
      return setNote(t('print.choose_a_canvas_slot_for_every'))
    const mapping = useCanvas ? slotted.map(tool => `${tool}:${map[tool]}`).join(',') : ''
    if (multi && !mapping && !free.length) return setNote(t('print.a_multicolour_file_requires_canvas'))
    if (stallHangs) return
    setBusy(true)
    setStall(null)
    await send(mapping, free, largeOk)
  }
  const send = async (mapping: string, free: ToolFilament[], large: boolean): Promise<void> => {
    if (launch === 'queue') {
      // The job keeps every choice above; the slicer's estimates are optional and read once here.
      setNote(t('print.adding_to_queue'))
      try {
        let estimate: any = null
        try {
          estimate = await post('/api/gcode-files/metadata', `${job.storage}\n${job.path}`)
        } catch {
          /* the queue shows no time for this job */
        }
        const numberOrNull = (v: unknown) => (typeof v === 'number' && Number.isFinite(v) && v >= 0 ? v : null)
        await addToQueue({
          storage: job.storage,
          path: job.path,
          mapping,
          side,
          leveling: calibrating ? 'calibrate' : 'saved',
          timelapse,
          measure: measureId,
          nozzle: nozzleId,
          copies,
          position,
          estimatedSeconds: numberOrNull(estimate?.estimated_seconds),
          filamentGrams: numberOrNull(estimate?.filament_grams),
          filaments: free,
          largeOk: large,
        })
        const g = activeGeneration
        activeGeneration = 0
        pending.set({ job: null })
        await clearOrcaPending(g)
        notify(tpl('queue.added_to_queue', { name: job.path }))
        void refreshQueue()
      } catch (err) {
        if (await confirmLarge(err)) return send(mapping, free, true)
        setNote(`${t('queue.not_added')} ${errText(err)}`)
        setBusy(false)
      }
      return
    }
    setNote(
      t(job.storage === 'usb' ? 'print.importing_usb_g_code_to_internal' : 'print.submitting_protected_print_request')
    )
    try {
      const result = await post(
        '/api/gcode-files/print',
        `${job.storage}\n${job.path}\n${mapping}\n${side}\n${calibrating ? 'calibrate' : 'saved'}\n${timelapse ? '1' : '0'}\n${measureId}\n${nozzleId}${large ? `\n${LARGE_OK}` : ''}`,
        startTimeoutMs(job.size)
      )
      // A confirmed file of 2 GiB or more: the dialog stays open until CC2 Control knows whether the printer began it.
      if (typeof result?.watch === 'number') {
        setNote(t('print.large_start_watching'))
        const outcome = await watchOutcome(result.watch)
        if (outcome.state === 'failed') {
          setNote(t('print.large_start_failed'))
          notify(t('print.large_start_failed'), 'error')
          setBusy(false)
          return
        }
      }
      const g = activeGeneration
      activeGeneration = 0
      pending.set({ job: null })
      await clearOrcaPending(g)
      notify(
        t(
          result?.imported_from_usb
            ? 'print.usb_import_completed_print_request'
            : 'print.print_request_submitted_waiting'
        )
      )
    } catch (err) {
      // The Canvas did not report its trays (before or after the copy): nothing was printed. The dialog stays open,
      // so the operator can start again, choose other spools or cancel.
      if (await confirmLarge(err)) return send(mapping, free, true)
      const reason = reasonOf(err)
      const stallSeq = Number((err as { data?: { stall?: number } })?.data?.stall)
      if (reason === 'usb_unresponsive' && stallSeq > 0) setStall(stallSeq)
      setNote(
        reason === 'canvas_unreported'
          ? t('print.canvas_unreported')
          : reason === 'usb_unresponsive'
            ? t(stallSeq > 0 ? 'print.usb_stall_hanging' : 'print.usb_unresponsive')
            : `${t('print.print_not_started')} ${errText(err)}`
      )
      setBusy(false)
    }
  }
  const label = 'my-3 flex items-center gap-2 text-[13px]'
  return (
    <Dialog onClose={close} locked={busy} width={820} label={t('print.choose_print_spool')}>
      <form onSubmit={submit}>
        <div class="text-xs text-muted">CANVAS · {t('print.print_setup')}</div>
        <DialogTitle class="my-2">{t('print.choose_print_spool')}</DialogTitle>
        <div class="mb-4 text-muted [overflow-wrap:anywhere]">{job.path}</div>
        <div class="my-3 grid grid-cols-2 gap-2 sm:grid-cols-4">
          {[0, 1, 2, 3].map(i => (
            <div key={i} class="rounded-lg border border-edge p-2 text-xs [overflow-wrap:anywhere]">
              <div class="mb-1.5 h-2 rounded" style={{ background: slotColour(tray(i), i) || 'var(--edge)' }} />
              <strong>{tpl('common.slot_n', { n: i + 1 })}</strong>
              <div>{slotLabel(tray(i))}</div>
              {slotEmptyNote(tray(i)) && <div class="text-muted">{slotEmptyNote(tray(i))}</div>}
            </div>
          ))}
        </div>
        <label class={label}>
          <input
            type="checkbox"
            checked={useCanvas}
            disabled={!job.connected || multi}
            onChange={e => {
              setUse(e.currentTarget.checked)
              setNote(
                t(
                  e.currentTarget.checked
                    ? 'print.assign_one_physical_canvas_slot_to'
                    : 'print.the_external_default_filament_path'
                )
              )
            }}
          />{' '}
          {t('print.use_elegoo_canvas')}
        </label>
        {job.tools.map(tool => (
          <fieldset key={tool} class="my-4 min-w-0 rounded-lg border border-edge p-3">
            <legend class="px-1.5 font-semibold">{tpl('print.filament_tool_n', { n: tool })}</legend>
            {(() => {
              const filament = job.filaments.find(f => f.tool === tool)
              const color = /^#[0-9a-f]{6}([0-9a-f]{2})?$/i.test(filament?.color || '') ? filament?.color : null
              return (
                <div class="mb-3 flex flex-wrap items-center gap-2 text-xs text-muted">
                  <span>{t('print.file_filament')}:</span>
                  {color && (
                    <span
                      class="inline-block size-4 shrink-0 rounded border border-edge"
                      style={{ background: color }}
                    />
                  )}
                  <span>
                    {[filament?.material, color].filter(Boolean).join(' · ') || t('print.file_filament_unknown')}
                  </span>
                </div>
              )
            })()}
            <div class="mb-2 text-xs text-muted">{t('print.choose_a_spool')}</div>
            <FeedChoice
              tool={tool}
              name={`tool-${tool}-slot`}
              trays={job.trays}
              value={map[tool] || ''}
              onChange={value => {
                setMap(m => ({ ...m, [tool]: value }))
                setNote(t('print.choose_the_spool_and_confirm_the'))
              }}
              disabled={!useCanvas || busy}
              dim={!useCanvas}
              stock={stock}
              stockOff={launch !== 'queue'}
            />
            {(() => {
              const s = supply(tool)
              if (!s) return null
              if (!s.spool) return <span class="text-xs text-muted">{t('spools.print_no_spool')}</span>
              const vars = { name: spoolLabel(s.spool), remaining: g(s.spool.remaining) }
              return s.need === null ? (
                <span class="text-xs text-muted">{tpl('spools.print_left', vars)}</span>
              ) : (
                <span class={s.need > s.spool.remaining ? 'text-xs text-red' : 'text-xs text-muted'}>
                  {tpl('spools.print_need', { ...vars, need: g(s.need) })}
                </span>
              )
            })()}
          </fieldset>
        ))}
        {short && <div class="my-2 text-[13px] text-red">{t('spools.print_short')}</div>}
        <div class="my-4 grid gap-3 sm:grid-cols-[1fr_1.35fr]">
          <fieldset class="rounded-lg border border-edge p-3">
            <legend class="px-1.5 font-semibold text-cyan">{t('print.build_plate_side')}</legend>
            {(['A', 'B'] as const).map(s => (
              <label key={s} class={label}>
                <input type="radio" name="plateSide" checked={side === s} onChange={() => setSide(s)} />{' '}
                {t(s === 'A' ? 'print.side_a' : 'print.side_b')}
              </label>
            ))}
            {unknown ? (
              <div class="grid gap-2 text-xs text-amber" data-mesh-readiness="unknown">
                <span role="status">
                  {tpl('bed.mesh_unavailable_error', { error: meshError || t('state.unknown') })}
                </span>
                <div>
                  <Button type="button" disabled={busy || meshChecking} onClick={recheckMesh}>
                    {t('files.refresh')}
                  </Button>
                </div>
              </div>
            ) : (
              <div class="text-xs text-amber" data-mesh-readiness={available ? 'saved' : 'missing'}>
                {t(available ? 'print.a_saved_mesh_is_available_for' : 'print.this_build_plate_side_has_no')}
              </div>
            )}
            {plateHere && !calibrating && (
              <label class="mt-2 grid gap-1 text-xs">
                {tpl('print.plate_mesh', { name: plateHere.name })}
                <Select value={measureId} disabled={busy} onChange={e => setMeasure(e.currentTarget.value)}>
                  {plateHere.measures.map(m => (
                    <option key={m.id} value={m.id} disabled={!usable(m)}>
                      {[
                        `${m.temp} °C`,
                        nozzleName(lib, m.nozzle),
                        m.slot ? t('print.measure_in_slot') : m.profile ? '' : t('print.measure_not_in_printer'),
                      ]
                        .filter(Boolean)
                        .join(' · ')}
                    </option>
                  ))}
                </Select>
                {job.bedTemp !== null && (
                  <span class="text-muted">{tpl('print.file_bed_temperature', { temp: job.bedTemp })}</span>
                )}
              </label>
            )}
            {mounted && !plateHere && (
              <div class="mt-2 text-xs text-muted">
                {tpl('print.mounted_other_side', { name: mounted.name, side: mounted.side })}
              </div>
            )}
            {!!lib?.nozzles.length && (
              <label class="mt-2 grid gap-1 text-xs">
                {t('common.nozzle')}
                <Select value={nozzleId} disabled={busy} onChange={e => setNozzle(e.currentTarget.value)}>
                  {!nozzleId && <option value="">{t('common.not_set')}</option>}
                  {lib.nozzles.map(n => (
                    <option key={n.id} value={n.id}>
                      {n.name}
                    </option>
                  ))}
                </Select>
                {chosenNozzle && job.nozzleDiameter !== null && !sameSize(chosenNozzle.diameter) && (
                  <span class="text-amber">
                    {tpl('print.file_nozzle_differs', { diameter: job.nozzleDiameter.toFixed(2) })}
                  </span>
                )}
              </label>
            )}
          </fieldset>
          <fieldset class="rounded-lg border border-edge p-3">
            <legend class="px-1.5 font-semibold text-cyan">{t('print.bed_preparation')}</legend>
            <label class={label}>
              <input
                type="checkbox"
                checked={calibrating}
                disabled={forced || busy || meshChecking}
                onChange={e => setCalibrate(e.currentTarget.checked)}
              />{' '}
              {t('print.calibrate_bed_before_printing')}
            </label>
            <div class="text-xs text-amber">
              {t(
                forced
                  ? 'print.a_complete_11_11_bed_mesh'
                  : unknown && !calibrating
                    ? 'canvas.state_unknown'
                    : calibrating
                      ? 'print.calibration_will_follow_the_g_code'
                      : 'print.the_printer_will_use_the_saved'
              )}
            </div>
          </fieldset>
        </div>
        <label class={label}>
          <input
            type="checkbox"
            checked={timelapse}
            disabled={busy}
            onChange={e => setTimelapse(e.currentTarget.checked)}
          />
          {t('print.enable_timelapse')}
        </label>
        <fieldset class="my-4 rounded-lg border border-cyan/60 p-3" data-print-launch>
          <legend class="px-1.5 font-semibold text-cyan">{t('print.launch')}</legend>
          <div class="flex flex-wrap items-center gap-3.5">
            <div
              role="radiogroup"
              aria-label={t('print.launch')}
              class="inline-flex gap-1 rounded-md border border-edge bg-field p-1"
            >
              <Button
                variant={launch === 'now' ? 'active' : 'ghost'}
                role="radio"
                aria-checked={launch === 'now'}
                disabled={busy}
                onClick={() => setLaunch('now')}
                data-launch="now"
              >
                <Play size={16} strokeWidth={1} />
                {t('print.run_now')}
              </Button>
              <Button
                variant={launch === 'queue' ? 'active' : 'ghost'}
                role="radio"
                aria-checked={launch === 'queue'}
                disabled={busy}
                onClick={() => setLaunch('queue')}
                data-launch="queue"
              >
                <ListOrdered size={16} strokeWidth={1} />
                {t('print.add_to_queue')}
              </Button>
            </div>
            {launch === 'queue' && (
              <div class="flex items-center gap-2.5">
                <span>{t('print.copies')}</span>
                <div class="inline-flex h-10 items-center rounded-md border border-edge bg-field">
                  <Button
                    variant="ghost"
                    class="min-h-9 px-2.5"
                    aria-label={t('queue.fewer_copies')}
                    disabled={busy || copies <= 1}
                    onClick={() => setCopies(copies - 1)}
                  >
                    <Minus size={16} strokeWidth={1} />
                  </Button>
                  <input
                    type="number"
                    min={1}
                    max={99}
                    value={copies}
                    aria-label={t('print.copies')}
                    disabled={busy}
                    class="h-9 w-16 border-x border-edge bg-transparent text-center text-base font-semibold text-fg"
                    onInput={e => {
                      const n = Number(e.currentTarget.value)
                      if (Number.isInteger(n) && n >= 1 && n <= 99) setCopies(n)
                    }}
                    data-print-copies
                  />
                  <Button
                    variant="ghost"
                    class="min-h-9 px-2.5"
                    aria-label={t('queue.more_copies')}
                    disabled={busy || copies >= 99}
                    onClick={() => setCopies(copies + 1)}
                  >
                    <Plus size={16} strokeWidth={1} />
                  </Button>
                </div>
              </div>
            )}
          </div>
          {launch === 'queue' ? (
            <>
              <div class="mt-2.5 flex flex-wrap gap-3">
                <label class="flex items-center gap-2">
                  <input
                    type="radio"
                    name="queuePosition"
                    checked={position === 'end'}
                    disabled={busy}
                    onChange={() => setPosition('end')}
                  />{' '}
                  {t('print.queue_position_end')}
                </label>
                <label class="flex items-center gap-2">
                  <input
                    type="radio"
                    name="queuePosition"
                    checked={position === 'next'}
                    disabled={busy}
                    onChange={() => setPosition('next')}
                  />{' '}
                  {t('print.queue_position_next')}
                </label>
              </div>
              <div class="mt-2 text-xs text-muted">{t('print.queue_copies_hint')}</div>
            </>
          ) : (
            <div class="mt-2 text-xs text-muted">{t('print.run_now_hint')}</div>
          )}
        </fieldset>
        <div class="min-h-7 text-[13px] text-muted" role="status" data-print-note>
          {stalled ? stallNote(stalled) : note}
        </div>
        <div class="mt-4 flex justify-end gap-2.5">
          <Button onClick={close} disabled={busy}>
            {t('common.cancel')}
          </Button>
          <Button
            type="submit"
            variant="primary"
            disabled={busy || meshChecking || stallHangs || (launch === 'now' && unknown && !calibrating)}
            data-print-submit
          >
            {launch === 'queue' ? tpl('print.add_to_queue_n', { n: copies }) : t('print.start_print')}
          </Button>
        </div>
      </form>
    </Dialog>
  )
}
