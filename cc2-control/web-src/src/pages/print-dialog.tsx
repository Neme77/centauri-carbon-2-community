import { useState } from 'preact/hooks'
import { store } from '@/lib/store'
import { Button } from '@/components/ui/button'
import { Select } from '@/components/ui/field'
import { Dialog } from '@/components/ui/dialog'
import { errText, notify, post, request } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { canvasColour, canvasModel } from '@/lib/canvas'
import { meshRoot } from '@/lib/mesh'
import { openPage } from '@/lib/state'

type Pending = {
  storage: string
  path: string
  tools: number[]
  meshAvailable: { A: boolean; B: boolean }
  trays: any[]
  connected: boolean
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

// Opens the confirmation dialog; a print is never started without the operator confirming the mapping.
export async function startFile(storage: string, path: string) {
  if (pending.get().job) return
  try {
    const [inspection, canvasData, meshData] = await Promise.all([
      post('/api/gcode-files/inspect', `${storage}\n${path}`),
      request('/api/canvas').catch(() => ({ available: false })),
      request('/api/mesh').catch(() => null),
    ])
    const tools: number[] = Array.isArray(inspection.tools) && inspection.tools.length ? inspection.tools : [0]
    const model = canvasModel(canvasData),
      profiles = meshRoot(meshData)?.profiles || {}
    pending.set({
      job: {
        storage,
        path,
        tools,
        meshAvailable: { A: Boolean(profiles.default), B: Boolean(profiles.default1) },
        trays: model?.trays || [],
        connected: Boolean(model?.connected),
      },
    })
  } catch (e) {
    pending.set({ job: null })
    notify(tpl('print.cannot_prepare_print_error', { error: errText(e) }), 'error')
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
        `${job.storage}\n${job.path}\n${mapping}\n${side}\n${calibrating ? 'calibrate' : 'saved'}\n${timelapse ? '1' : '0'}`
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
    <Dialog onClose={close} locked={busy} width={680}>
      <form onSubmit={submit}>
        <div class="text-xs text-muted">CANVAS · {t('print.print_setup')}</div>
        <h2 class="my-2 text-xl font-semibold">{t('print.choose_print_spool')}</h2>
        <div class="mb-4 text-muted [overflow-wrap:anywhere]">{job.path}</div>
        <div class="my-3 grid grid-cols-2 gap-2 sm:grid-cols-4">
          {[0, 1, 2, 3].map(i => (
            <div key={i} class="rounded-lg border border-edge p-2 text-xs [overflow-wrap:anywhere]">
              <div class="mb-1.5 h-2 rounded" style={{ background: canvasColour(tray(i)?.filament_color, i) }} />
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
          <label key={tool} class="my-3 grid grid-cols-[1fr_2fr] items-center gap-3">
            <strong>{tpl('print.filament_tool_n', { n: tool })}</strong>
            <Select
              disabled={!useCanvas}
              value={map[tool] || ''}
              onChange={e => {
                const v = e.currentTarget.value
                setMap(m => ({ ...m, [tool]: v }))
              }}
            >
              <option value="">{t('print.choose_a_spool')}</option>
              {[0, 1, 2, 3].map(i => (
                <option key={i} value={i}>
                  {tpl('common.slot_n', { n: i + 1 })} · {slotLabel(tray(i))}
                </option>
              ))}
            </Select>
          </label>
        ))}
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
