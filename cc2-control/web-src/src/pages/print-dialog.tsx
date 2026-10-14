import { useState } from 'preact/hooks'
import { store } from '@/lib/store'
import { Button } from '@/components/ui/button'
import { Dialog } from '@/components/ui/dialog'
import { Select } from '@/components/ui/field'
import { errText, notify, post, request, toast } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { canvasHex, canvasModel } from '@/lib/canvas'
import { meshRoot } from '@/lib/mesh'
import { nearestMeasure, nozzleName, type PlateLibrary, usable } from '@/lib/plates'
import { openPage, printer } from '@/lib/state'
import { EXTERNAL, findSpool, g, spoolGrams, spoolLabel, type SpoolLibrary } from '@/lib/spools'

type Pending = {
  storage: string
  path: string
  tools: number[]
  // mm: the slicer's filament length for the tool, when the file states it.
  filaments: { tool: number; color: string; material: string; mm?: number }[]
  meshAvailable: { A: boolean; B: boolean }
  trays: any[]
  connected: boolean
  spools: SpoolLibrary | null
  // The build-plate library, and the file's first-layer bed temperature and nozzle diameter when it states them.
  plates: PlateLibrary | null
  bedTemp: number | null
  nozzleDiameter: number | null
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
  if (pending.get().job || fileAnalysisPending) return
  fileAnalysisPending = true
  notify(t('print.analysing_file'))
  try {
    const [inspection, canvasData, meshData, library, plateLibrary] = await Promise.all([
      post('/api/gcode-files/inspect', `${storage}\n${path}`),
      request('/api/canvas').catch(() => ({ available: false })),
      request('/api/mesh').catch(() => null),
      printer.get().data?.spools?.enabled ? request('/api/spools').catch(() => null) : null,
      request('/api/plates').catch(() => null),
    ])
    const tools: number[] = Array.isArray(inspection.tools) && inspection.tools.length ? inspection.tools : [0]
    const model = canvasModel(canvasData),
      profiles = meshRoot(meshData)?.profiles || {}
    pending.set({
      job: {
        storage,
        path,
        tools,
        filaments: Array.isArray(inspection.filaments) ? inspection.filaments : [],
        meshAvailable: { A: Boolean(profiles.default), B: Boolean(profiles.default1) },
        trays: model?.trays || [],
        connected: Boolean(model?.connected),
        spools: library?.available && library.enabled ? library : null,
        plates: plateLibrary?.available ? plateLibrary : null,
        bedTemp: typeof inspection.bed_temperature === 'number' ? inspection.bed_temperature : null,
        nozzleDiameter: typeof inspection.nozzle_diameter === 'number' ? inspection.nozzle_diameter : null,
      },
    })
    toast.set(s => ({ ...s, text: '' }))
  } catch (e) {
    pending.set({ job: null })
    notify(tpl('print.cannot_prepare_print_error', { error: errText(e) }), 'error')
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

// Missing Canvas colours are unknown, not the shared helper's decorative fallback.
const slotColour = (tray: any, index: number) => {
  const value = tray?.filament_color
  if (value === undefined || value === null || value === '') return null
  if (Array.isArray(value) && value.length >= 3 && value.slice(0, 3).every(Number.isFinite))
    return canvasHex(value, index)
  if (typeof value === 'number' && Number.isFinite(value)) return canvasHex(value, index)
  if (typeof value === 'string' && /^(?:#?[0-9a-f]{6}(?:[0-9a-f]{2})?|0x[0-9a-f]{6,8})$/i.test(value.trim()))
    return canvasHex(value, index)
  return null
}

const slotLabel = (tray: any) => (tray && (tray.filament_name || tray.filament_type)) || t('print.not_reported')

export const PrintDialog = () => {
  const job = pending.use().job
  return job ? <Form job={job} /> : null
}

const Form = ({ job }: { job: Pending }) => {
  const multi = job.tools.length > 1
  const [useCanvas, setUse] = useState(job.connected)
  const [map, setMap] = useState<Record<number, string>>({})
  const [side, setSide] = useState<'A' | 'B'>('A')
  const [calibrate, setCalibrate] = useState(false)
  const [timelapse, setTimelapse] = useState(false)
  const [busy, setBusy] = useState(false)
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
  const available = job.meshAvailable[side],
    forced = !available,
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
    const slot = useCanvas ? (map[tool] === undefined || map[tool] === '' ? -1 : Number(map[tool])) : EXTERNAL
    if (!lib || slot < 0) return null
    const spool = findSpool(lib, lib.slots[slot]?.spool || '')
    const mm = Number(job.filaments.find(f => f.tool === tool)?.mm)
    return { spool, need: spool && mm > 0 ? spoolGrams(spool, mm) : null }
  }
  const short = job.tools.some(tool => {
    const s = supply(tool)
    return s?.spool && s.need !== null && s.need > s.spool.remaining
  })

  const close = () => {
    if (busy) return
    const g = activeGeneration
    activeGeneration = 0
    pending.set({ job: null })
    void clearOrcaPending(g)
  }
  const submit = async (e: Event) => {
    e.preventDefault()
    if (busy) return
    if (useCanvas && job.tools.some(tool => !map[tool])) return setNote(t('print.choose_a_canvas_slot_for_every'))
    const mapping = useCanvas ? job.tools.map(tool => `${tool}:${map[tool]}`).join(',') : ''
    if (multi && !mapping) return setNote(t('print.a_multicolour_file_requires_canvas'))
    setBusy(true)
    setNote(
      t(job.storage === 'usb' ? 'print.importing_usb_g_code_to_internal' : 'print.submitting_protected_print_request')
    )
    try {
      const result = await post(
        '/api/gcode-files/print',
        `${job.storage}\n${job.path}\n${mapping}\n${side}\n${calibrating ? 'calibrate' : 'saved'}\n${timelapse ? '1' : '0'}\n${measureId}\n${nozzleId}`
      )
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
      setNote(`${t('print.print_not_started')} ${errText(err)}`)
      setBusy(false)
    }
  }
  const label = 'my-3 flex items-center gap-2 text-[13px]'
  return (
    <Dialog onClose={close} locked={busy} width={820}>
      <form onSubmit={submit}>
        <div class="text-xs text-muted">CANVAS · {t('print.print_setup')}</div>
        <h2 class="my-2 text-xl font-semibold">{t('print.choose_print_spool')}</h2>
        <div class="mb-4 text-muted [overflow-wrap:anywhere]">{job.path}</div>
        <div class="my-3 grid grid-cols-2 gap-2 sm:grid-cols-4">
          {[0, 1, 2, 3].map(i => (
            <div key={i} class="rounded-lg border border-edge p-2 text-xs [overflow-wrap:anywhere]">
              <div class="mb-1.5 h-2 rounded" style={{ background: slotColour(tray(i), i) || 'var(--edge)' }} />
              <strong>{tpl('common.slot_n', { n: i + 1 })}</strong>
              <div>{slotLabel(tray(i))}</div>
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
            <div class="grid grid-cols-2 gap-2 sm:grid-cols-4">
              {[0, 1, 2, 3].map(i => {
                const color = slotColour(tray(i), i)
                const selected = map[tool] === String(i)
                return (
                  <label
                    key={i}
                    class={`flex min-w-0 items-start gap-2 rounded-lg border p-2 text-xs ${selected && useCanvas ? 'border-cyan bg-cyan/10' : 'border-edge'} ${useCanvas ? 'cursor-pointer' : 'opacity-50'}`}
                  >
                    <input
                      type="radio"
                      name={`tool-${tool}-slot`}
                      value={String(i)}
                      checked={selected}
                      disabled={!useCanvas || busy}
                      onChange={e => {
                        setMap(m => ({ ...m, [tool]: e.currentTarget.value }))
                        setNote(t('print.choose_the_spool_and_confirm_the'))
                      }}
                      class="mt-0.5 shrink-0"
                    />
                    <span class="grid min-w-0 gap-1 [overflow-wrap:anywhere]">
                      <strong>{tpl('common.slot_n', { n: i + 1 })}</strong>
                      <span class="flex items-center gap-1.5">
                        {color && (
                          <span
                            class="inline-block size-4 shrink-0 rounded border border-edge"
                            style={{ background: color }}
                          />
                        )}
                        <span>{slotLabel(tray(i))}</span>
                      </span>
                      <span class="text-muted">{color || t('print.not_reported')}</span>
                    </span>
                  </label>
                )
              })}
            </div>
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
            <div class="text-xs text-amber">
              {t(available ? 'print.a_saved_mesh_is_available_for' : 'print.this_build_plate_side_has_no')}
            </div>
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
                disabled={forced}
                onChange={e => setCalibrate(e.currentTarget.checked)}
              />{' '}
              {t('print.calibrate_bed_before_printing')}
            </label>
            <div class="text-xs text-amber">
              {t(
                forced
                  ? 'print.a_complete_11_11_bed_mesh'
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
        <div class="min-h-7 text-[13px] text-muted" role="status">
          {note}
        </div>
        <div class="mt-4 flex justify-end gap-2.5">
          <Button onClick={close} disabled={busy}>
            {t('common.cancel')}
          </Button>
          <Button type="submit" variant="primary" disabled={busy}>
            {t('print.start_print')}
          </Button>
        </div>
      </form>
    </Dialog>
  )
}
