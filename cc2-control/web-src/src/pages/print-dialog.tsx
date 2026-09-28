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

type Pending = { storage: string; path: string; tools: number[]; meshAvailable: { A: boolean; B: boolean }; trays: any[]; connected: boolean }
const pending = store({ job: null as Pending | null })
let activeGeneration = 0, lastSeen = 0, opening = false

async function clearOrcaPending(generation: number) {
  if (!generation) return
  try { await post('/api/orca/pending-print/clear', String(generation)) } catch (e) { notify(tpl('Cannot clear OrcaSlicer request: {error}', { error: errText(e) }), 'error') }
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
    const model = canvasModel(canvasData), profiles = meshRoot(meshData)?.profiles || {}
    pending.set({ job: { storage, path, tools, meshAvailable: { A: Boolean(profiles.default), B: Boolean(profiles.default1) }, trays: model?.trays || [], connected: Boolean(model?.connected) } })
  } catch (e) { pending.set({ job: null }); notify(tpl('Cannot prepare print: {error}', { error: errText(e) }), 'error') }
}

// An OrcaSlicer "Upload and Print" queues a confirmation here, not an unattended print.
export async function checkOrcaPendingPrint() {
  if (opening || pending.get().job) return
  opening = true // lock before the request so overlapping polls cannot open two dialogs
  try {
    const job = await request('/api/orca/pending-print')
    if (!job?.pending || !job.filename || !job.generation || job.generation === lastSeen) return
    await startFile('internal', job.filename)
    if (pending.get().job?.path === job.filename) {
      activeGeneration = lastSeen = job.generation
      openPage('files')
      notify(t('OrcaSlicer upload complete. Choose the Canvas spool and confirm to start printing.'))
    }
  } catch { /* keep the request pending for a retry */ } finally { opening = false }
}

const slotLabel = (tray: any) => tray && (tray.filament_name || tray.filament_type) || t('Not reported')

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
  const [busy, setBusy] = useState(false)
  const [note, setNote] = useState(job.connected ? 'Choose the spool and confirm the print.' : multi ? 'Canvas not detected: a multicolour print cannot start.' : 'Canvas not detected: external/default filament path selected.')
  const tray = (i: number) => job.trays.find(x => Number(x.tray_id) === i) || job.trays[i]
  const available = job.meshAvailable[side], forced = !available, calibrating = forced || calibrate

  const close = () => { if (busy) return; const g = activeGeneration; activeGeneration = 0; pending.set({ job: null }); void clearOrcaPending(g) }
  const submit = async (e: Event) => {
    e.preventDefault()
    if (busy) return
    if (useCanvas && job.tools.some(tool => !map[tool])) return setNote('Choose a Canvas slot for every filament.')
    const mapping = useCanvas ? job.tools.map(tool => `${tool}:${map[tool]}`).join(',') : ''
    if (multi && !mapping) return setNote('A multicolour file requires Canvas slot mapping.')
    setBusy(true)
    setNote(job.storage === 'usb' ? 'Importing USB G-code to internal storage and submitting print…' : 'Submitting protected print request…')
    try {
      const result = await post('/api/gcode-files/print', `${job.storage}\n${job.path}\n${mapping}\n${side}\n${calibrating ? 'calibrate' : 'saved'}`)
      const g = activeGeneration; activeGeneration = 0; pending.set({ job: null })
      await clearOrcaPending(g)
      notify(t(result?.imported_from_usb ? 'USB import completed; print request submitted.' : 'Print request submitted. Waiting for printer status.'))
    } catch (err) { setNote(`${t('Print not started:')} ${errText(err)}`); setBusy(false) }
  }
  const label = 'my-3 flex items-center gap-2 text-[13px]'
  return (
    <Dialog onClose={close} locked={busy} width={680}>
      <form onSubmit={submit}>
        <div class="text-xs text-muted">CANVAS · {t('Print setup')}</div>
        <h2 class="my-2 text-xl font-semibold">{t('Choose print spool')}</h2>
        <div class="mb-4 text-muted [overflow-wrap:anywhere]">{job.path}</div>
        <div class="my-3 grid grid-cols-2 gap-2 sm:grid-cols-4">
          {[0, 1, 2, 3].map(i => <div key={i} class="rounded-lg border border-edge p-2 text-xs [overflow-wrap:anywhere]"><div class="mb-1.5 h-2 rounded" style={{ background: canvasColour(tray(i)?.filament_color, i) }} /><strong>Slot {i + 1}</strong><div>{slotLabel(tray(i))}</div></div>)}
        </div>
        <label class={label}><input type="checkbox" checked={useCanvas} disabled={!job.connected || multi} onChange={e => { setUse(e.currentTarget.checked); setNote(e.currentTarget.checked ? 'Assign one physical Canvas slot to each G-code filament.' : 'The external/default filament path will be used.') }} /> {t('Use ELEGOO Canvas')}</label>
        {job.tools.map(tool => (
          <label key={tool} class="my-3 grid grid-cols-[1fr_2fr] items-center gap-3"><strong>Filament T{tool}</strong>
            <Select disabled={!useCanvas} value={map[tool] || ''} onChange={e => { const v = e.currentTarget.value; setMap(m => ({ ...m, [tool]: v })) }}>
              <option value="">{t('Choose a spool')}</option>{[0, 1, 2, 3].map(i => <option key={i} value={i}>Slot {i + 1} · {slotLabel(tray(i))}</option>)}
            </Select></label>
        ))}
        <div class="my-4 grid gap-3 sm:grid-cols-[1fr_1.35fr]">
          <fieldset class="rounded-lg border border-edge p-3"><legend class="px-1.5 font-semibold text-cyan">{t('Build plate side')}</legend>
            {(['A', 'B'] as const).map(s => <label key={s} class={label}><input type="radio" name="plateSide" checked={side === s} onChange={() => setSide(s)} /> {t(`Side ${s}`)}</label>)}
            <div class="text-xs text-amber">{t(available ? 'A saved mesh is available for this build plate side.' : 'This build plate side has no saved mesh. Calibration is required.')}</div></fieldset>
          <fieldset class="rounded-lg border border-edge p-3"><legend class="px-1.5 font-semibold text-cyan">{t('Bed preparation')}</legend>
            <label class={label}><input type="checkbox" checked={calibrating} disabled={forced} onChange={e => setCalibrate(e.currentTarget.checked)} /> {t('Calibrate bed before printing')}</label>
            <div class="text-xs text-amber">{t(forced ? 'A complete 11 × 11 bed mesh will be measured before printing.' : calibrating ? 'Calibration will follow the G-code: adaptive when supported, otherwise full-bed.' : 'The printer will use the saved mesh for the selected side.')}</div></fieldset>
        </div>
        <div class="min-h-7 text-[13px] text-muted" role="status">{t(note)}</div>
        <div class="mt-4 flex justify-end gap-2.5"><Button onClick={close} disabled={busy}>{t('Cancel')}</Button><Button type="submit" variant="primary" disabled={busy}>{t('Start print')}</Button></div>
      </form>
    </Dialog>
  )
}
