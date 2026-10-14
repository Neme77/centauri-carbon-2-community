import { useEffect, useMemo, useRef, useState } from 'preact/hooks'
import { ask } from '@/lib/confirm'
import { ArrowBigDown, ArrowBigRight, ArrowBigUp, ArrowLeftRight, Sigma } from 'lucide-preact'
import { cn } from '@/lib/utils'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Dot, Tag } from '@/components/ui/badge'
import { Input, Select } from '@/components/ui/field'
import { Tabs } from '@/components/ui/tabs'
import { Icon } from '@/components/icons'
import { Notice } from '@/components/shared'
import { control, errText, notify, request } from '@/lib/api'
import { type Key, t, tpl } from '@/lib/i18n'
import { signed } from '@/lib/format'
import { matrixFromUds, meshRoot, type Pt } from '@/lib/mesh'
import { defaultCam, drawMesh, meshStats, type Cam } from '@/lib/meshdraw'
import { microns, screwPlan, screwValues } from '@/lib/screws'
import { usePoll } from '@/lib/poll'
import { type BedTab, printer, nav, openPage, refreshConsole, screwText } from '@/lib/state'
import {
  type Calibration,
  calibrate,
  findMeasure,
  meshProfile,
  nozzleName,
  plateBase,
  type PlateLibrary,
  platePoints,
  plates,
  refreshPlates,
  stopCalibration,
  tempOk,
} from '@/lib/plates'
import { NozzleSelect, Plates } from '@/pages/bed-plates'

const I = { size: 16, strokeWidth: 1 }
const PROFILES: [string, Key][] = [
  ['default', 'bed.side_a_default'],
  ['default1', 'bed.side_b_default1'],
  ['ADAPTIVE', 'bed.adaptive_mesh_adaptive'],
]
type View = '3d' | '2d' | 'values'

const MeshCard = () => {
  const [data, setData] = useState<any>(null)
  const { profile } = meshProfile.use()
  const setProfile = (value: string) => meshProfile.set({ profile: value })
  const library = plates.use().data
  const found = profile.startsWith('measure:') ? findMeasure(library, profile.slice(8)) : null
  // A deleted measurement falls back to the printer's active mesh; an older `plate:` choice shows the plate's mesh.
  useEffect(() => {
    if (!library) return
    if (profile.startsWith('plate:')) {
      const p = library.plates.find(x => `plate:${x.id}` === profile)
      setProfile(p ? `measure:${plateBase(p).id}` : 'active')
    } else if (profile.startsWith('measure:') && !found) setProfile('active')
  }, [library, profile, found])
  const [view, setView] = useState<View>('3d')
  const [scale, setScale] = useState(1)
  const cam = useRef<Cam>(defaultCam())
  const canvas = useRef<HTMLCanvasElement>(null)
  const busy = useRef(false)
  const queued = useRef(false)
  const alive = useRef(true)
  const points: Pt[] = useMemo(
    () => (found ? platePoints(found.measure.mesh) : (data && matrixFromUds(data, profile)) || []),
    [data, profile, found]
  )
  const shown = found
    ? tpl('plates.measure_option', { name: found.plate.name, temp: found.measure.temp })
    : `${t('bed.saved_profile')} · ${profile}`
  const xs = [...new Set(points.map(p => p.x))].sort((a, b) => a - b)
  const ys = [...new Set(points.map(p => p.y))].sort((a, b) => b - a)
  const cells = new Map(points.map(p => [`${p.x},${p.y}`, p.z]))
  const st = meshStats(points),
    root = meshRoot(data)

  const redraw = () => {
    if (canvas.current && view !== 'values')
      drawMesh(canvas.current, points, view, cam.current, {
        empty: t('bed.waiting_for_live_mesh_data'),
        min: t('bed.min'),
        max: t('bed.max'),
        back: t('bed.y_increases_towards_the_back'),
      })
  }
  useEffect(redraw, [points, view, scale])
  useEffect(() => {
    const el = canvas.current
    if (!el) return
    const observer = new ResizeObserver(redraw)
    observer.observe(el)
    return () => observer.disconnect()
  }, [points, view])

  const warned = useRef(false) // one error toast per outage, not one every 5 s
  const load = async (manual = false) => {
    if (busy.current) {
      queued.current = true
      return
    }
    busy.current = true
    try {
      const d = await request('/api/mesh')
      if (!meshRoot(d)) throw Error(t('bed.the_firmware_did_not_expose_bed'))
      setData(d)
      warned.current = false
    } catch (e) {
      if (manual === true || !warned.current) notify(tpl('bed.mesh_unavailable_error', { error: errText(e) }), 'error')
      warned.current = true
    } finally {
      busy.current = false
      if (queued.current && alive.current) {
        queued.current = false
        void load()
      }
    }
  }
  useEffect(() => {
    alive.current = true
    void load()
    void refreshPlates() // saved plates are offered next to the printer profiles
    return () => {
      alive.current = false
    }
  }, [])

  // Reuse cached printer status; query the mesh only when the print/calibration
  // phase changes, never on each status poll or while this page is unmounted.
  const current = printer.use().data
  const phase = `${current?.machine?.status}/${current?.machine?.sub_status}/${current?.print?.uuid}`
  const lastPhase = useRef(phase)
  useEffect(() => {
    if (phase !== lastPhase.current) {
      lastPhase.current = phase
      void load()
    }
  }, [phase])

  const names = PROFILES.filter(([n]) => root?.profiles?.[n])
  const drag = useRef<{ id: number; x: number; y: number } | null>(null)
  useEffect(() => {
    const el = canvas.current
    if (!el) return
    const wheel = (e: WheelEvent) => {
      e.preventDefault()
      cam.current.zoom = Math.max(0.6, Math.min(1.5, cam.current.zoom * (e.deltaY > 0 ? 0.95 : 1.05)))
      redraw()
    }
    el.addEventListener('wheel', wheel, { passive: false })
    return () => el.removeEventListener('wheel', wheel)
  }, [points, view])

  const Stat = ({ dot, label, val }: { dot: any; label: Key; val: string }) => (
    <div class="cc2-mesh-stat rounded-lg border border-edge bg-field/60 p-2.5">
      <span class="flex items-center gap-2 text-xs">
        {dot}
        {t(label)}
      </span>
      <strong class="mt-2 block whitespace-nowrap text-2xl font-semibold">
        {val} <small class="text-xs">mm</small>
      </strong>
      <p class="mt-1 text-xs text-muted">{profile === 'active' ? t('bed.current_printer_mesh') : shown}</p>
    </div>
  )
  return (
    <Card>
      <div class="mb-3 flex min-h-7 flex-wrap items-center gap-3 border-b border-edge pb-2.5">
        <Icon n="cube" class="text-cyan" />
        <div class="mr-auto">
          <h2 class="text-base font-semibold">{t('bed.bed_mesh_3d')}</h2>
          <small class="text-muted">
            {points.length ? tpl('bed.n_live_probe_points', { n: points.length }) : t('bed.waiting_for_live_mesh_data')}
          </small>
        </div>
        <label class="flex items-center gap-2 text-xs">
          {t('bed.mesh_profile')}
          <Select class="w-auto" value={profile} onChange={e => setProfile(e.currentTarget.value)}>
            <option value="active">
              {t('bed.active_mesh')}
              {root?.profile_name ? ` · ${root.profile_name}` : ''}
            </option>
            {names.map(([n, l]) => (
              <option key={n} value={n}>
                {t(l)}
              </option>
            ))}
            {library?.plates.map(p => (
              <optgroup key={p.id} label={tpl('plates.plate_option', { name: p.name })}>
                {p.measures.map(m => (
                  <option key={m.id} value={`measure:${m.id}`}>
                    {[`${m.temp} °C`, nozzleName(library, m.nozzle)].filter(Boolean).join(' · ')}
                  </option>
                ))}
              </optgroup>
            ))}
          </Select>
        </label>
        <div class="flex">
          {(['3d', '2d', 'values'] as View[]).map((v, i) => (
            <Button
              key={v}
              variant={view === v ? 'primary' : 'default'}
              class={cn('text-xs', i === 0 ? 'rounded-r-none' : i === 2 ? 'rounded-l-none' : 'rounded-none')}
              onClick={() => setView(v)}
            >
              {t(v === '3d' ? 'bed.view_3d' : v === '2d' ? 'bed.view_2d' : 'bed.values')}
            </Button>
          ))}
        </div>
        <label class="flex items-center gap-2 border-l border-edge pl-3 text-xs">
          {t('bed.z_scale')}
          <input
            class="w-18"
            type="range"
            min=".3"
            max="2"
            step=".1"
            value={scale}
            onInput={e => {
              cam.current.scale = +e.currentTarget.value
              setScale(cam.current.scale)
            }}
          />
          <span>{scale.toFixed(1)}×</span>
        </label>
      </div>
      <div class="overflow-hidden rounded-lg bg-well">
        {view === 'values' ? (
          <div class="h-[430px] overflow-auto p-3 font-mono text-xs text-well-fg">
            {points.length < 4 ? (
              <p class="text-muted">{t('bed.waiting_for_live_mesh_data_2')}</p>
            ) : (
              <table class="w-full border-collapse">
                <caption class="mb-2 text-left">{t('bed.live_z_heights_in_mm_y')}</caption>
                <thead>
                  <tr>
                    <th class="p-1 text-left font-normal">Y / X</th>
                    {xs.map(x => (
                      <th key={x} class="p-1 text-left font-normal">
                        {Number(x.toFixed(2))}
                      </th>
                    ))}
                  </tr>
                </thead>
                <tbody>
                  {ys.map(y => (
                    <tr key={y}>
                      <th class="p-1 text-left">{Number(y.toFixed(2))}</th>
                      {xs.map(x => (
                        <td key={x} class="p-1">
                          {cells.get(`${x},${y}`)?.toFixed(3) ?? '—'}
                        </td>
                      ))}
                    </tr>
                  ))}
                </tbody>
              </table>
            )}
          </div>
        ) : (
          <canvas
            ref={canvas}
            aria-label={t('bed.interactive_live_bed_mesh')}
            class="block h-[430px] w-full cursor-grab touch-none active:cursor-grabbing"
            onPointerDown={e => {
              drag.current = { id: e.pointerId, x: e.clientX, y: e.clientY }
              e.currentTarget.setPointerCapture(e.pointerId)
            }}
            onPointerMove={e => {
              const d = drag.current
              if (!d || d.id !== e.pointerId) return
              cam.current.yaw += (e.clientX - d.x) * 0.005
              cam.current.pitch = Math.max(0.25, Math.min(1.2, cam.current.pitch + (e.clientY - d.y) * 0.004))
              drag.current = { id: e.pointerId, x: e.clientX, y: e.clientY }
              redraw()
            }}
            onPointerUp={() => {
              drag.current = null
            }}
            onPointerCancel={() => {
              drag.current = null
            }}
          />
        )}
        <div class="p-2 text-center text-xs text-muted">
          {t('bed.drag_to_rotate_scroll_to_zoom')}{' '}
          <button
            type="button"
            class="text-well-fg underline underline-offset-2"
            onClick={() => {
              cam.current = defaultCam()
              setScale(1)
              redraw()
            }}
          >
            {t('bed.reset_view')}
          </button>
        </div>
      </div>
      <div class="mt-3 grid grid-cols-2 gap-2.5 cc2-md:grid-cols-4">
        <Stat dot={<Dot c="blue" />} label="bed.minimum" val={st ? signed(st.min) : '—'} />
        <Stat dot={<Dot c="amber" />} label="bed.maximum" val={st ? signed(st.max) : '—'} />
        <Stat
          dot={<ArrowLeftRight {...I} class="text-cyan" />}
          label="bed.range"
          val={st ? st.range.toFixed(3) : '—'}
        />
        <Stat dot={<Sigma {...I} class="text-cyan" />} label="bed.average" val={st ? signed(st.mean) : '—'} />
      </div>
      <MeshActions
        reload={() => load(true)}
        note={
          points.length
            ? profile === 'active'
              ? `${t('bed.active_mesh_loaded')}${root?.profile_name ? ` · ${root.profile_name}` : ''}.`
              : found
                ? `${shown}.`
                : `${t('bed.saved_mesh_loaded')} · ${profile}.`
            : t('bed.waiting_for_the_printer_mesh')
        }
      />
    </Card>
  )
}

// An unhomed printer is homed first.
const homedXYZ = (axes: unknown) => typeof axes === 'string' && ['x', 'y', 'z'].every(a => axes.includes(a))
const soakOk = (text: string) => /^\d{1,2}$/.test(text.trim()) && Number(text) <= 60
const clock = (seconds: number) => `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}`
const FAILED: Record<string, Key> = {
  homing: 'bed.homing_did_not_finish',
  heating: 'bed.cal_failed_heating',
  busy: 'bed.cal_failed_busy',
  telemetry: 'bed.cal_failed_telemetry',
  probing: 'bed.cal_failed_probing',
}

// What the calibration CC2 Control runs on the printer is doing; it can be stopped until the probing starts.
const CalibrationProgress = ({ job, lib }: { job: Calibration; lib: PlateLibrary }) => {
  const [stopping, setStopping] = useState(false)
  const text =
    job.stage === 'homing'
      ? t('bed.cal_homing')
      : job.stage === 'heating'
        ? tpl('bed.cal_heating', { bed: job.bed === null ? '—' : Math.round(job.bed), temp: job.temp })
        : job.stage === 'soaking'
          ? tpl('bed.cal_soaking', { temp: job.temp, left: clock(job.remaining) })
          : job.stage === 'probing'
            ? tpl('bed.cal_probing', { side: job.side, temp: job.temp })
            : tpl('bed.cal_saving', { name: lib.plates.find(p => p.id === job.plate)?.name || '' })
  return (
    <Notice icon="bolt">
      <span class="flex flex-wrap items-center gap-x-3 gap-y-1" data-calibration={job.stage}>
        <span>{text}</span>
        <small class="text-muted">{t('bed.cal_runs_on_printer')}</small>
        {['homing', 'heating', 'soaking'].includes(job.stage) && (
          <Button
            class="ml-auto min-h-7 px-2 text-xs"
            disabled={stopping}
            onClick={async () => {
              setStopping(true)
              try {
                await stopCalibration()
              } catch (e) {
                notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
              } finally {
                setStopping(false)
                void refreshPlates()
              }
            }}
          >
            {t('bed.cal_stop')}
          </Button>
        )}
      </span>
    </Notice>
  )
}

const MeshActions = ({ reload, note }: { reload: () => void; note: string }) => {
  const [side, setSide] = useState('')
  const [temp, setTemp] = useState('60')
  const [soak, setSoak] = useState('10')
  const [nozzle, setNozzle] = useState<string | null>(null)
  // The plate the result is saved to: null follows the mounted plate of that side, '' saves to none.
  const [target, setTarget] = useState<string | null>(null)
  const [starting, setStarting] = useState(false)
  const library = plates.use().data
  const job = library?.calibration
  const running = Boolean(job && job.stage !== 'off')
  // The calibration runs on the printer; the page only follows it, faster while it runs.
  usePoll(refreshPlates, running ? 2000 : 30000)
  const plateSide = side === 'default1' ? 'B' : side === 'default' ? 'A' : null
  const candidates = library?.available ? library.plates.filter(p => p.side === plateSide) : []
  const plateId =
    target !== null && (target === '' || candidates.some(p => p.id === target))
      ? target
      : candidates.find(p => p.id === library?.current)?.id || ''
  const nozzleId = nozzle ?? library?.nozzle ?? ''
  const busy = starting || running
  const lock = useRef(false)
  // The end of a run seen by this page: the new mesh is read once and the outcome told.
  const seen = useRef<string | null>(null)
  useEffect(() => {
    if (!job) return
    const before = seen.current
    seen.current = job.stage
    if (before === null || before === 'off' || job.stage !== 'off') return
    reload()
    const plate = library?.plates.find(p => p.id === job.plate)
    if (job.result === 'saved') notify(tpl('bed.measure_saved', { name: plate?.name || '', temp: job.temp }))
    else if (job.result === 'done') notify(t('bed.cal_done'))
    else if (job.result === 'cancelled') notify(t('bed.cal_cancelled'))
    else if (job.result === 'failed' && job.error === 'saving')
      notify(tpl('bed.measure_not_saved', { error: job.detail }), 'error')
    else if (job.result === 'failed') notify(t(FAILED[job.error] || 'bed.cal_failed_probing'), 'error')
  }, [job?.stage])
  const start = async () => {
    if (!side || !plateSide || lock.current || running) return
    if (!tempOk(temp)) return notify(t('common.invalid_bed_temperature'), 'error')
    if (!soakOk(soak)) return notify(t('bed.invalid_soak'), 'error')
    lock.current = true
    setStarting(true)
    try {
      const degrees = Number(temp),
        minutes = Number(soak)
      const plate = candidates.find(p => p.id === plateId)
      const lines = [t(side === 'default1' ? 'bed.side_b_default1' : 'bed.side_a_default')]
      const nozzleLabel = nozzleName(library, nozzleId)
      if (nozzleLabel) lines.push(tpl('bed.calibration_uses_nozzle', { name: nozzleLabel }))
      if (plate) lines.push(tpl('bed.calibration_saves_to', { name: plate.name, temp: degrees }))
      if (minutes) lines.push(tpl('bed.calibration_soak', { temp: degrees, minutes }))
      const homing = homedXYZ(printer.get().data?.motion?.homed_axes) ? '' : `\n\n${t('bed.calibration_homes_first')}`
      const question = tpl('bed.start_a_new_bed_mesh_calibration', { temp: degrees })
      if (!(await ask(`${question}\n\n${lines.join('\n')}${homing}`))) return
      await calibrate(plateSide, degrees, minutes, nozzleId, plate?.id || '')
      await refreshPlates()
    } catch (e) {
      notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
    } finally {
      lock.current = false
      setStarting(false)
    }
  }
  return (
    <>
      <div class="mt-3 grid gap-2 text-xs cc2-sm:grid-cols-2 cc2-lg:grid-cols-[1.3fr_7rem_6rem_1fr_1.3fr]">
        <label class="grid gap-1">
          {t('bed.calibration_plate_side')}
          <Select value={side} disabled={busy} onChange={e => setSide(e.currentTarget.value)}>
            <option value="">{t('bed.choose_calibration_side')}</option>
            {PROFILES.slice(0, 2).map(([profile, label]) => (
              <option key={profile} value={profile}>
                {t(label)}
              </option>
            ))}
          </Select>
        </label>
        <label class="grid gap-1">
          {t('common.bed_temperature_c')}
          <Input
            type="number"
            min="40"
            max="110"
            step="1"
            value={temp}
            disabled={busy}
            onInput={e => setTemp(e.currentTarget.value)}
          />
        </label>
        <label class="grid gap-1">
          {t('bed.soak_minutes')}
          <Input
            type="number"
            min="0"
            max="60"
            step="1"
            value={soak}
            disabled={busy}
            onInput={e => setSoak(e.currentTarget.value)}
          />
        </label>
        {library && (
          <label class="grid gap-1">
            {t('common.nozzle')}
            <NozzleSelect lib={library} value={nozzleId} disabled={busy} onChange={setNozzle} />
          </label>
        )}
        {library?.available && (
          <label class="grid gap-1">
            {t('bed.calibration_plate')}
            <Select value={plateId} disabled={busy || !plateSide} onChange={e => setTarget(e.currentTarget.value)}>
              <option value="">{t('bed.no_plate')}</option>
              {candidates.map(p => (
                <option key={p.id} value={p.id}>
                  {p.name}
                </option>
              ))}
            </Select>
          </label>
        )}
      </div>
      {running && job && library && <CalibrationProgress job={job} lib={library} />}
      <div class="cc2-mesh-actions mt-3 grid gap-3 cc2-sm:grid-cols-[1fr_1.15fr]">
        {[
          ['folder', 'bed.load_current_mesh', 'bed.read_the_saved_mesh_from_printer', reload, false],
          ['bolt', 'bed.run_bed_mesh_calibration', 'bed.start_a_protected_calibration_when', start, true],
        ].map(([icon, title, sub, fn, primary]: any) => (
          <Button
            key={title}
            variant={primary ? 'primary' : 'default'}
            class="h-auto items-start justify-start gap-3.5 p-3 text-left"
            onClick={fn}
            disabled={primary && (!side || busy)}
          >
            <Icon n={icon} class="size-7" />
            <span>
              <strong class="block text-sm">{t(title)}</strong>
              <small class="mt-1 block whitespace-normal text-xs font-normal">{t(sub)}</small>
            </span>
          </Button>
        ))}
      </div>
      <Notice>{note}</Notice>
    </>
  )
}

const Screws = () => {
  const measuring = useRef(false)
  usePoll(refreshConsole, 2500) // the measurement result arrives in the console output
  const values = screwValues(screwText.use().text)
  const plan = values ? screwPlan(values) : null
  const rows: [string, string, string][] = [
    ['FL', '30,30', '1'],
    ['FR', '230,30', '2'],
    ['RR', '230,225', '3'],
    ['RL', '30,225', '4'],
  ]
  const plate: [number, string, string, string][] = [
    [3, '4', 'RL', 'X30 Y225'],
    [2, '3', 'RR', 'X230 Y225'],
    [0, '1', 'FL', 'X30 Y30'],
    [1, '2', 'FR', 'X230 Y30'],
  ]
  return (
    <Card id="screwFocus" class="flex-1">
      <CardHead
        icon="target"
        title="bed.four_screw_leveling"
        end={<Tag tone="warning">{t(plan ? 'bed.measured_results' : 'bed.no_measurement')}</Tag>}
      />
      <p class="mb-3 text-xs text-muted">{t('bed.nozzle_load_cell_measurement_at')}</p>
      <div class="grid gap-3 cc2-sm:grid-cols-[.9fr_1.1fr]">
        <div class="rounded-lg border border-edge p-3">
          <div class="relative grid h-48 grid-cols-2 grid-rows-2 rounded-xl border-2 border-muted">
            <i class="absolute inset-y-2 left-1/2 border-l border-dashed border-edge" />
            <i class="absolute inset-x-2 top-1/2 border-t border-dashed border-edge" />
            {plate.map(([i, n, name, xy]) => {
              const v = plan ? plan.shown[i] : 0,
                tint = plan
                  ? `rgba(${v > 0 ? '20,207,233' : '255,123,134'},${(0.06 + Math.min(1, Math.abs(v) / 0.2) * 0.2).toFixed(2)})`
                  : undefined
              return (
                <div
                  key={n}
                  class="relative flex flex-col items-center justify-center rounded-lg text-xs"
                  style={{ background: tint }}
                >
                  <b class="mb-0.5 grid size-6.5 place-items-center rounded-full border border-muted font-medium">
                    {n}
                  </b>
                  {name}
                  <small class="text-xs text-muted">{xy}</small>
                  {plan && <span class="mt-0.5 text-[13px] font-bold">{microns(v)}</span>}
                </div>
              )
            })}
          </div>
          <div class="mt-1.5 text-center text-xs text-muted">{t('bed.top_view_front_edge_at_bottom')}</div>
          <div class="mt-2 flex justify-between text-xs text-muted">
            <span class="flex items-center gap-1">
              <ArrowBigUp size={12} strokeWidth={1} /> Y (back)
            </span>
            <span class="flex items-center gap-1">
              <ArrowBigRight size={12} strokeWidth={1} /> X (right)
            </span>
          </div>
        </div>
        <div>
          <div class="overflow-hidden rounded-md border border-edge">
            <table class="w-full border-collapse text-xs">
              <thead>
                <tr class="text-left text-xs text-muted">
                  <th class="p-2 font-normal">{t('common.position')}</th>
                  <th class="p-2 font-normal">{t('bed.offset')}</th>
                  <th class="p-2 font-normal">{t('bed.adjustment')}</th>
                </tr>
              </thead>
              <tbody>
                {rows.map(([name], i) => {
                  const d = plan?.shown[i] ?? 0,
                    within = Math.abs(d) <= 0.02,
                    ref = plan && !plan.useOptimized && i === 0
                  return (
                    <tr key={name} class={cn('border-t border-edge', plan?.useOptimized && i === 0 && 'bg-amber/10')}>
                      <td class="p-2 font-semibold">{name}</td>
                      <td class="p-2">{plan ? (ref ? t('bed.reference') : microns(d)) : '—'}</td>
                      <td class={cn('p-2', !within && plan && !ref && (d > 0 ? 'text-cyan' : 'text-red'))}>
                        {plan ? (
                          ref ? (
                            '—'
                          ) : within ? (
                            t('bed.within_tolerance')
                          ) : d > 0 ? (
                            <span class="flex items-center gap-1">
                              <ArrowBigDown {...I} />
                              {t('bed.lower')}
                            </span>
                          ) : (
                            <span class="flex items-center gap-1">
                              <ArrowBigUp {...I} />
                              {t('bed.raise')}
                            </span>
                          )
                        ) : (
                          '—'
                        )}
                      </td>
                    </tr>
                  )
                })}
              </tbody>
            </table>
          </div>
          <Notice>
            <span>
              {t('bed.3_samples_per_point')}
              <br />
              {t('bed.front_left_reference')}
              <br />
              {t('bed.mesh_capture_stays_separate')}
            </span>
          </Notice>
        </div>
      </div>
      {plan?.useOptimized && (
        <Notice icon="info">
          {tpl('bed.optimized_reference_adjustment', {
            word: t(plan.common > 0 ? 'bed.lower_2' : 'bed.raise_2'),
            amount: Math.abs(Math.round(plan.common * 1000)),
            before: plan.before,
            best: plan.best,
          })}
        </Notice>
      )}
      <div class="mt-3">
        <Button
          onClick={async () => {
            if (measuring.current) return
            measuring.current = true
            try {
              if (!(await ask(t('bed.start_the_four_screw_load_cell')))) return
              const previous = await request('/api/console')
              screwText.set({ text: '', minGeneration: Number(previous.generation) + 1 })
              if (await control('screws:measure')) openPage('bed', 'screws')
            } catch (e) {
              notify(errText(e), 'error')
            } finally {
              measuring.current = false
            }
          }}
        >
          <Icon n="target" class="size-5" />
          {t('bed.measure_screws')}
        </Button>
      </div>
    </Card>
  )
}

export const Bed = () => {
  const { bed } = nav.use()
  return (
    <Page title="common.bed_levelling" sub="bed.mesh_saved_profiles_and_four_screw">
      <Tabs
        items={[
          { id: 'mesh', label: t('bed.mesh_saved_profiles'), icon: <Icon n="grid" /> },
          { id: 'plates', label: t('bed.build_plates'), icon: <Icon n="layers" /> },
          { id: 'screws', label: t('bed.screw_levelling'), icon: <Icon n="target" /> },
        ]}
        value={bed}
        onChange={id => openPage('bed', id as BedTab)}
      />
      {bed === 'screws' ? (
        <div class="max-w-4xl">
          <Screws />
        </div>
      ) : bed === 'plates' ? (
        <Plates />
      ) : (
        <MeshCard />
      )}
    </Page>
  )
}
