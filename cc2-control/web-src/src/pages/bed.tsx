import { CalibrationTools } from './calibration-tools'
import { useEffect, useMemo, useRef, useState } from 'preact/hooks'
import { ask, type ConfirmText } from '@/lib/confirm'
import {
  ArrowBigRight,
  ArrowBigUp,
  ArrowDown,
  ArrowLeftRight,
  ArrowUp,
  Box,
  ChartColumn,
  Check,
  Grid2x2,
  Grid3x3,
  Layers,
  Lightbulb,
  RotateCcw,
  RotateCw,
  Sigma,
  TriangleAlert,
  Wrench,
} from 'lucide-preact'
import { InfoHint } from '@/components/info-hint'
import { ScrewHistoryTable } from '@/components/screw-history-table'
import { cn } from '@/lib/utils'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Dot, Tag } from '@/components/ui/badge'
import { Input, Select } from '@/components/ui/field'
import { Tabs } from '@/components/ui/tabs'
import { Icon } from '@/components/icons'
import { Notice, Warn } from '@/components/shared'
import { control, errText, notify, post, request } from '@/lib/api'
import { i18n, type Key, t, tpl } from '@/lib/i18n'
import { signed } from '@/lib/format'
import { matrixFromUds, meshRoot, type Pt } from '@/lib/mesh'
import { defaultCam, drawMesh, type Mark, meshStats, stops as meshStops, type Cam } from '@/lib/meshdraw'
import {
  cornerReads,
  heightLabel,
  type HeightUnit,
  PROBE_SPREAD_LIMIT_UM,
  probeSpreadUm,
  TOLERANCE_UM,
  TURN_LIMIT_UM,
  parseHeightUnit,
  parseTurnMode,
  parseTurnUnit,
  clockHours,
  TURN_MODES,
  TURN_UNITS,
  CLICK_DEG,
  turnExact,
  turnFraction,
  turnFractionAid,
  turnsNumber,
  turnPlan,
  type TurnMode,
  type TurnUnit,
  type TurnStage,
  mergeCornerMeasure,
  SCREW_CORNERS,
  SCREW_KEYS,
  SCREW_SESSION_TIMEOUT_MIN,
  SCREW_SETTLE_S,
  referenceMove,
  measuredOffsets,
  screwHistoryCsv,
  screwPlan,
  screwVerdict,
  SCREW_VERDICT_TONE,
  screwsTurnedSince,
  screwValues,
  stripSoak,
  stuckCorners,
  type ScrewId,
  withSoak,
} from '@/lib/screws'
import { screwSoakOf } from '@/lib/screw-soak'
import { chooseOptimized, keepStandard, returnToStandard, screwStrategy } from '@/lib/screw-strategy'
import { refreshScrewHistory, screwHistory } from '@/lib/screw-history'
import { usePoll } from '@/lib/poll'
import { ls, store } from '@/lib/store'
import { beginChain, chainTellsCancel, guidedChain, SCREW_JOB, setScrewStarter } from '@/lib/guided-chain'
import { LEVEL_MODE_KEY, levelMesh, levelWork, setLevelMesh, takeActiveCopy, workDone } from '@/lib/level-state'
import {
  type BedTab,
  consoleStatus,
  printer,
  nav,
  openPage,
  refreshConsole,
  refreshPrinter,
  screwSession,
  screwText,
} from '@/lib/state'
import {
  calibrate,
  calibrationForm,
  findMeasure,
  meshProfile,
  nozzleName,
  plateBase,
  platePoints,
  plates,
  refreshPlates,
  tempOk,
} from '@/lib/plates'
import { FAILED, homedXYZ, SOAK_MAX_MIN, soakOk } from '@/components/calibration-progress'
import {
  ALIGN_DEFAULTS,
  analyseMesh,
  analyseScrewProbes,
  carryMesh,
  QUICK_TWIST_WARN_MM,
  turnsConfigured,
} from '@/lib/mesh-align'
import { NozzleSelect, Plates } from '@/pages/bed-plates'
import { NozzleExpansion } from '@/pages/nozzle-expansion'
import { type TempRow, TempValues } from '@/components/temp-values'
import { StatusMark } from '@/components/screw-verdict'
import { NumMark } from '@/components/num-mark'
import { Step } from '@/components/step'
import { BasisStep, useBasis } from '@/pages/bed-basis'
import { AlignSettingsBlock, mm, SHAPE, ShapeMap, signedMm, Tile, tiltWords, useAlignConfig } from '@/pages/bed-align'
import { formatLiveValue, liveNumber } from '@/lib/temp-values'

const I = { size: 16, strokeWidth: 1 }
const PROFILES: [string, Key][] = [
  ['default', 'bed.side_a_default'],
  ['default1', 'bed.side_b_default1'],
  ['ADAPTIVE', 'bed.adaptive_mesh_adaptive'],
]
const profileLabel = (name: string) => {
  const known = PROFILES.find(([id]) => id === name)
  return known ? t(known[1]) : name
}
type View = '3d' | '2d' | 'values'

// The Mesh tab's 3D view of the chosen mesh, with the four screws marked; drag to rotate.
const AlignMesh3d = ({
  mesh,
  screws,
  mode,
  active,
  onPick,
}: {
  mesh: any
  screws: { id: string; x: number; y: number }[]
  mode: '3d' | '2d'
  // The corner picked in the table (its mark is highlighted); a click on a mark picks its corner (display only).
  active?: string | null
  onPick?: (id: ScrewId) => void
}) => {
  const [zScale, setZScale] = useState(2)
  const canvas = useRef<HTMLCanvasElement>(null)
  const cam = useRef<Cam>({ ...defaultCam(), scale: 2 })
  const drag = useRef<{ id: number; x: number; y: number } | null>(null)
  const points = useMemo(() => platePoints(mesh), [mesh])
  const stats = meshStats(points)
  const marks = useRef<(Mark & { id: string })[]>([])
  marks.current = screws.map(s => ({
    id: s.id,
    x: s.x,
    y: s.y,
    label: String(SCREW_CORNERS.find(c => c.id === s.id)?.n ?? ''),
    active: s.id === active,
  }))
  const down = useRef<{ x: number; y: number } | null>(null)
  const redraw = () => {
    if (canvas.current)
      drawMesh(
        canvas.current,
        points,
        mode,
        cam.current,
        { empty: '', min: t('bed.min'), max: t('bed.max'), back: t('bed.y_increases_towards_the_back') },
        marks.current
      )
  }
  useEffect(redraw, [points, mode, zScale, active])
  useEffect(() => {
    const el = canvas.current
    if (!el) return
    const observer = new ResizeObserver(redraw)
    observer.observe(el)
    const wheel = (e: WheelEvent) => {
      e.preventDefault()
      cam.current.zoom = Math.max(0.6, Math.min(3, cam.current.zoom * (e.deltaY > 0 ? 0.95 : 1.05)))
      redraw()
    }
    el.addEventListener('wheel', wheel, { passive: false })
    return () => {
      observer.disconnect()
      el.removeEventListener('wheel', wheel)
    }
  }, [points, mode])
  return (
    <div class="overflow-hidden rounded-lg bg-well">
      {mode === '3d' && (
        <label class="flex min-w-0 items-center gap-2 pt-2 pr-12 pl-3 text-xs text-well-fg">
          {t('bed.z_scale')}
          <input
            class="min-w-0 flex-1"
            type="range"
            min=".3"
            max="4"
            step=".1"
            value={zScale}
            onInput={e => {
              cam.current.scale = +e.currentTarget.value
              setZScale(cam.current.scale)
            }}
          />
          <span class="w-8 text-right">{zScale.toFixed(1)}×</span>
        </label>
      )}
      <canvas
        ref={canvas}
        class="block aspect-square w-full cursor-grab touch-none active:cursor-grabbing"
        onPointerDown={e => {
          drag.current = { id: e.pointerId, x: e.clientX, y: e.clientY }
          down.current = { x: e.clientX, y: e.clientY }
          e.currentTarget.setPointerCapture(e.pointerId)
        }}
        onPointerMove={e => {
          const d = drag.current
          if (!d || d.id !== e.pointerId) return
          // The button was released somewhere else: stop following the mouse.
          if (e.buttons === 0) {
            drag.current = null
            return
          }
          if (mode === '2d') {
            // 2D: drag moves the zoomed map, so every corner can be reached.
            cam.current.panX = (cam.current.panX ?? 0) + (e.clientX - d.x)
            cam.current.panY = (cam.current.panY ?? 0) + (e.clientY - d.y)
          } else {
            cam.current.yaw += (e.clientX - d.x) * 0.005
            cam.current.pitch = Math.max(0.25, Math.min(1.2, cam.current.pitch + (e.clientY - d.y) * 0.004))
          }
          drag.current = { id: e.pointerId, x: e.clientX, y: e.clientY }
          redraw()
        }}
        onDblClick={() => {
          cam.current = { ...defaultCam(), scale: cam.current.scale }
          redraw()
        }}
        onPointerUp={e => {
          drag.current = null
          // A click (no drag) on a mark picks its corner.
          const start = down.current
          down.current = null
          if (!onPick || !start || Math.hypot(e.clientX - start.x, e.clientY - start.y) > 4) return
          const r = e.currentTarget.getBoundingClientRect()
          const hit = marks.current.find(
            m => m.sx != null && m.sy != null && Math.hypot(e.clientX - r.left - m.sx, e.clientY - r.top - m.sy) <= 14
          )
          if (hit) onPick(hit.id as ScrewId)
        }}
        onPointerCancel={() => {
          drag.current = null
        }}
        onLostPointerCapture={() => {
          drag.current = null
        }}
      />
      {stats && (
        <div class="flex items-center gap-2 px-3 pb-2 text-xs text-well-fg/80" data-level-mesh-legend>
          <span>{signedMm(stats.min)} mm</span>
          <i
            class="h-2 flex-1 rounded-full"
            style={{ background: `linear-gradient(90deg, ${meshStops.map(c => `rgb(${c.join(',')})`).join(', ')})` }}
          />
          <span>{signedMm(stats.max)} mm</span>
        </div>
      )}
    </div>
  )
}

const MeshCard = () => {
  const [data, setData] = useState<any>(null)
  const { profile } = meshProfile.use()
  const setProfile = (value: string) => meshProfile.set({ profile: value })
  const library = plates.use().data
  const found = profile.startsWith('measure:') ? findMeasure(library, profile.slice(8)) : null
  // A saved measurement that is older than the newest screw measurement may show the bed as it was before the screws
  // were turned. Only measurements of the plate library carry a time; the printer's own profiles do not.
  const screwTrail = screwHistory.use()
  useEffect(() => void refreshScrewHistory(), [])
  const lastScrewTime = screwTrail.sessions.reduce((max, x) => Math.max(max, ...x.measurements.map(m => m.time)), 0)
  const meshBeforeScrews = Boolean(found && lastScrewTime > 0 && found.measure.measured < lastScrewTime)
  // A deleted measurement falls back to the printer's active mesh; an older `plate:` choice shows the plate's mesh.
  useEffect(() => {
    if (!library) return
    if (profile.startsWith('plate:')) {
      const p = library.plates.find(x => `plate:${x.id}` === profile)
      const base = p ? plateBase(p) : null
      setProfile(base ? `measure:${base.id}` : 'active')
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
    : `${t('bed.saved_profile')} · ${profileLabel(profile)}`
  const xs = [...new Set(points.map(p => p.x))].sort((a, b) => a - b)
  const ys = [...new Set(points.map(p => p.y))].sort((a, b) => b - a)
  const cells = new Map(points.map(p => [`${p.x},${p.y}`, p.z]))
  const st = meshStats(points),
    root = meshRoot(data)
  // The printer has answered and applies no mesh (an empty profile_name and probed_matrix): its nozzle cleaning (G180)
  // unloads the mesh, so after every screw measurement, until a print loads its side's mesh again. That is said instead
  // of waiting; without an answer yet (or a failed read) the page still waits.
  const unloaded = profile === 'active' && !found && Boolean(root) && points.length === 0 && !root?.profile_name

  const redraw = () => {
    if (canvas.current && view !== 'values')
      drawMesh(canvas.current, points, view, cam.current, {
        empty: unloaded ? '' : t('bed.waiting_for_live_mesh_data'),
        min: t('bed.min'),
        max: t('bed.max'),
        back: t('bed.y_increases_towards_the_back'),
      })
  }
  useEffect(redraw, [points, view, scale, unloaded])
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
      {/* Same head as every other card (CardHead): chip with a 16 px icon, same gap; it only adds the mesh note and the choices. */}
      <div class="cc2-card-head mb-3 flex min-h-7 flex-wrap items-center gap-2.5 border-b border-edge pb-2.5">
        <span class="cc2-card-chip">
          <Icon n="cube" class="size-4" />
        </span>
        <div class="min-w-0">
          <h2 class="text-base font-semibold">{t('bed.bed_mesh_3d')}</h2>
          {!unloaded && (
            <small class="text-muted">
              {points.length
                ? tpl('bed.n_live_probe_points', { n: points.length })
                : t('bed.waiting_for_live_mesh_data')}
            </small>
          )}
        </div>
        {/* Which mesh is shown, between the title and the profile choice. */}
        <span
          class="mx-auto flex items-center gap-1.5 rounded-md border border-edge bg-field/60 px-2.5 py-1 text-xs text-muted"
          data-mesh-note
        >
          <Icon n="info" class="size-4 text-cyan" />
          {points.length
            ? profile === 'active'
              ? `${t('bed.active_mesh_loaded')}${root?.profile_name ? ` · ${profileLabel(root.profile_name)}` : ''}`
              : found
                ? shown
                : `${t('bed.saved_mesh_loaded')} · ${profileLabel(profile)}`
            : unloaded
              ? t('bed.mesh_unloaded')
              : t('bed.waiting_for_the_printer_mesh')}
        </span>
        <label class="flex items-center gap-2 text-xs">
          {t('bed.mesh_profile')}
          <Select class="w-auto" value={profile} onChange={e => setProfile(e.currentTarget.value)}>
            <option value="active">
              {t('bed.active_mesh')}
              {root?.profile_name ? ` · ${profileLabel(root.profile_name)}` : ''}
            </option>
            {names.map(([n, l]) => (
              <option key={n} value={n}>
                {t(l)}
              </option>
            ))}
            {(library?.plates ?? []).map(p => (
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
      <CalibrationTools panel="cell" inline />
      {meshBeforeScrews && (
        <div class="mb-2" data-mesh-before-screws>
          <Warn>{tpl('bed.mesh_before_screws', { time: new Date(lastScrewTime * 1000).toLocaleString() })}</Warn>
        </div>
      )}
      <div class="relative overflow-hidden rounded-lg bg-well">
        {/* No mesh loaded: the hint in the empty area of the 3D and 2D views (the canvas draws no text then). */}
        {unloaded && view !== 'values' && (
          <p
            class="pointer-events-none absolute inset-x-0 top-[180px] px-6 text-center text-xs text-well-fg/70"
            data-mesh-unloaded
          >
            {t('bed.mesh_unloaded')}
          </p>
        )}
        {view === 'values' ? (
          <div class="h-[430px] overflow-auto p-3 font-mono text-xs text-well-fg">
            {points.length < 4 ? (
              <p class="text-well-fg/70" data-mesh-unloaded={unloaded ? '' : undefined}>
                {t(unloaded ? 'bed.mesh_unloaded' : 'bed.waiting_for_live_mesh_data_2')}
              </p>
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
        <div class="p-2 text-center text-xs text-well-fg/70">
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
      <MeshActions reload={() => load(true)} />
    </Card>
  )
}

const MeshActions = ({ reload }: { reload: () => void }) => {
  const [starting, setStarting] = useState(false)
  const library = plates.use().data
  const job = library?.calibration
  const running = Boolean(job && job.stage !== 'off')
  const draft = calibrationForm.use()
  useEffect(() => {
    if (running && job)
      calibrationForm.set({
        side: job.side === 'B' ? 'default1' : 'default',
        temp: String(job.temp),
        soak: String(job.soak / 60),
        nozzle: job.nozzle,
        target: job.plate,
      })
  }, [running, job?.side, job?.temp, job?.soak, job?.nozzle, job?.plate])
  const { side, temp, soak, nozzle, target } =
    running && job
      ? {
          side: job.side === 'B' ? 'default1' : 'default',
          temp: String(job.temp),
          soak: String(job.soak / 60),
          nozzle: job.nozzle,
          target: job.plate,
        }
      : draft
  const setSide = (side: string) => calibrationForm.set({ side, target: null })
  const setTemp = (temp: string) => calibrationForm.set({ temp })
  const setSoak = (soak: string) => calibrationForm.set({ soak })
  const setNozzle = (nozzle: string) => calibrationForm.set({ nozzle })
  const setTarget = (target: string) => calibrationForm.set({ target })
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
  // A screw session is open: a measurement of it that is running blocks the start (the program answers 409 as well); between
  // its measurements the session ends by itself when the calibration starts.
  const sessionOpen = Boolean(printer.use().data?.screws_session?.active)
  const sessionMeasuring = sessionOpen && consoleStatus.use().busy
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
    else if (job.result === 'work') notify(tpl('bed.work_measured', { name: plate?.name || '', temp: job.temp }))
    else if (job.result === 'done') notify(t('bed.cal_done'))
    // The stop of the guided chain's mesh is told by the chain (on every page), not twice.
    else if (job.result === 'cancelled' && !chainTellsCancel()) notify(t('bed.cal_cancelled'))
    // A run with a plate that was measured but not saved has the note at the top (components/guided-chain), not a toast.
    else if (job.result === 'failed' && job.error === 'saving') {
      if (job.work) notify(tpl('bed.work_not_read', { error: job.detail }), 'error')
      else if (!job.plate) notify(tpl('bed.measure_not_saved', { error: job.detail }), 'error')
    } else if (job.result === 'failed') notify(t(FAILED[job.error] || 'bed.cal_failed_probing'), 'error')
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
      if (sessionOpen) lines.push(t('bed.cal_ends_session'))
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
            min="30"
            max="110"
            step="1"
            value={temp}
            disabled={busy}
            onInput={e => setTemp(e.currentTarget.value)}
          />
        </label>
        <label class="grid gap-1">
          {t('bed.soak_minutes')}
          <Select value={soak} disabled={busy} onChange={e => setSoak(e.currentTarget.value)}>
            {Array.from({ length: SOAK_MAX_MIN + 1 }, (_, minutes) => (
              <option key={minutes} value={String(minutes)}>
                {minutes}
              </option>
            ))}
          </Select>
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
            disabled={primary && (!side || busy || sessionMeasuring)}
            title={primary && sessionMeasuring ? t('bed.cal_blocked_session') : undefined}
          >
            <Icon n={icon} class="size-7" />
            <span>
              <strong class="block text-sm">{t(title)}</strong>
              <small class="mt-1 block whitespace-normal text-xs font-normal">{t(sub)}</small>
            </span>
          </Button>
        ))}
      </div>
      {sessionMeasuring && (
        <p class="mt-2 text-xs text-muted" data-cal-blocked>
          {t('bed.cal_blocked_session')}
        </p>
      )}
    </>
  )
}

const saveText = (name: string, text: string, type: string) => {
  const url = URL.createObjectURL(new Blob([text], { type }))
  const link = document.createElement('a')
  link.href = url
  link.download = name
  document.body.appendChild(link)
  link.click()
  link.remove()
  window.setTimeout(() => URL.revokeObjectURL(url), 1000)
}

// The measurement this page started itself: its kind, the corner, when (performance.now(), like printer.at)
// and whether the console was seen busy since. The service reports only "busy", no phase and no start
// time, so after a reload the page knows none of this and says so instead of counting.
type ScrewJob = {
  kind: 'start' | 'probe' | 'check' | 'quick' | null
  corner: ScrewId | null
  at: number
  sawBusy: boolean
}
const screwJob = store<ScrewJob>({ kind: null, corner: null, at: 0, sawBusy: false })
const jobOf = (sent: string): Pick<ScrewJob, 'kind' | 'corner'> => {
  const action = stripSoak(sent)
  const probe = /^screws:tram:probe:(FL|FR|RR|RL)/.exec(action)
  if (probe) return { kind: 'probe', corner: probe[1] as ScrewId }
  if (action.startsWith('screws:tram:start')) return { kind: 'start', corner: null }
  if (action.startsWith('screws:tram:check')) return { kind: 'check', corner: null }
  if (action.startsWith('screws:measure')) return { kind: 'quick', corner: null }
  return { kind: null, corner: null }
}
// Sends a screw action: the stores the result is read into start over, then the action. `values`: the shown corner
// heights, which a one-corner measurement is merged into. `quiet`: a refusal is not told but thrown back (the guided
// chain after a new mesh reads its reason and tries again while the printer is busy). True when the printer took it.
// With a hold time in front (lib/screws withSoak) the same holds for the action behind it.
const sendScrewAction = async (sent: string, values: Record<string, number> | null, quiet: boolean) => {
  const action = stripSoak(sent)
  if (action.startsWith('screws:measure:corner:'))
    screwQuick.set({ base: values, corner: /corner:(FL|FR|RR|RL)/.exec(action)?.[1] as ScrewId })
  else if (action.startsWith('screws:measure')) screwQuick.set({ base: null, corner: null })
  const started = jobOf(action)
  if (started.kind) screwJob.set({ ...started, at: performance.now(), sawBusy: false })
  try {
    const previous = await request('/api/console')
    const nextGen = Number(previous.generation) + 1
    if (action.startsWith('screws:tram:start') || action.startsWith('screws:measure')) {
      screwText.set({ text: '', minGeneration: nextGen })
      screwSession.set({ heights: {}, appliedGeneration: 0, minGeneration: nextGen })
    } else screwSession.set({ minGeneration: nextGen })
    if (quiet) await post('/api/control', sent)
    else if (!(await control(sent))) {
      screwJob.set({ kind: null, corner: null, sawBusy: false })
      return false
    }
    await Promise.all([refreshPrinter(), refreshConsole(), refreshScrewHistory()])
    return true
  } catch (e) {
    screwJob.set({ kind: null, corner: null, sawBusy: false })
    throw e
  }
}
// The guided chain (lib/guided-chain) runs in the frame of the app and starts the screw points through this page's sender.
setScrewStarter((action, quiet) => sendScrewAction(action, null, quiet))
// The last full quick measurement and the corner a one-corner quick measurement re-measured: the page merges
// that corner's result into the full one (lib/screws mergeCornerMeasure).
const screwQuick = store<{ base: Record<string, number> | null; corner: ScrewId | null }>({ base: null, corner: null })
// A job that never showed as busy (the printer service answered at once) is forgotten after this long.
const SCREW_JOB_FORGET_MS = 15_000

// The unit the corner heights are shown in; micrometres unless the operator chose millimetres (kept in this browser).
const SCREW_UNIT_KEY = 'cc2-screw-height-unit'
const screwUnit = store<{ unit: HeightUnit }>({ unit: parseHeightUnit(ls.get(SCREW_UNIT_KEY)) })
// How the turn is shown: the mode (Auto, Coarse, Fine with the tool) and the unit (degrees, clicks, fractions,
// clock). Both are kept in this browser. The screw is the CC2's own M4: 0.7 mm per turn (not yet re-measured).
const SCREW_TURN_MODE_KEY = 'cc2-screw-turn-mode'
// A bed up to this much above its target starts as it is (the printer regulates from there); above it the start cools first.
const BED_HOT_LIMIT_K = 2.5
const SCREW_TURN_UNIT_KEY = 'cc2-screw-turn-unit'
const screwTurn = store<{ mode: TurnMode; unit: TurnUnit }>({
  mode: parseTurnMode(ls.get(SCREW_TURN_MODE_KEY)),
  unit: parseTurnUnit(ls.get(SCREW_TURN_UNIT_KEY)),
})
const CARRIER_TILT_HINT_MM = 1
const TURN_MODE_LABEL: Record<TurnMode, Key> = {
  auto: 'bed.turn_mode_auto',
  exact: 'bed.turn_mode_exact',
}
const TURN_UNIT_LABEL: Record<TurnUnit, Key> = {
  degrees: 'bed.turn_unit_degrees',
  clicks: 'bed.turn_unit_clicks',
  fractions: 'bed.turn_unit_fractions',
  clock: 'bed.turn_unit_clock',
  clockhours: 'bed.turn_unit_clock_hours',
}
const CLICK_KEYS: Record<string, Key> = {
  one: 'bed.turn_clicks_one',
  few: 'bed.turn_clicks_few',
  many: 'bed.turn_clicks_many',
  other: 'bed.turn_clicks_other',
}
const clicksText = (count: string, plural: string) => tpl(CLICK_KEYS[plural] ?? CLICK_KEYS.other, { n: count })
const pluralOf = (n: number, lang: string) => {
  try {
    return new Intl.PluralRules(lang).select(n)
  } catch {
    return 'other'
  }
}

// The points of a start button as one line; each point stays a point of its own, separated by " · " (the evaluation
// with a mesh is appended to the way of measuring this way).
const Bullets = ({ items }: { items: string[] }) => (
  <p class="mt-1 text-xs font-normal break-words" data-screw-entry-points>
    {items.join(' · ')}
  </p>
)

// The start buttons of step 1 ("Measure only (quick)", "Start session", "Measure a new mesh") have one height, room for a
// title and two lines below it, so nothing jumps when the way of measuring changes ("!": the Button's own min-h-9 stands
// later in the style sheet and would win).
const START_BUTTON = 'h-auto min-h-[4.625rem]! items-start justify-start px-3 py-2 text-left'
// Step 1: the two ways of measuring. "With mesh" is a tick beside them (lib/level-state levelMesh), not a third way.
type StartMode = 'quick' | 'session'

const Levelling = () => {
  const measuring = useRef(false)
  const [pending, setPending] = useState(false)
  const [probeTwice, setProbeTwice] = useState(true)
  // Step 1: how to measure, quick or with a session; the last choice is kept in this browser (an earlier "mesh" was
  // turned into "quick" with the tick by lib/level-state).
  const [startMode, pickMode] = useState<StartMode>(() => (ls.get(LEVEL_MODE_KEY) === 'session' ? 'session' : 'quick'))
  const setStartMode = (m: StartMode) => {
    pickMode(m)
    ls.set(LEVEL_MODE_KEY, m)
  }
  // "With mesh": the evaluation of both ways takes the plate shape of the chosen mesh off. While the chain after a new
  // mesh runs it stays ticked, so its screw points are evaluated with that mesh also after a reload.
  const meshTicked = levelMesh.use().on
  // Step 1: basis (with the tick a saved mesh, the temporary mesh or the temperature of a new mesh; without it a
  // material or a typed temperature) and measurement start; it uses that temperature.
  const basis = useBasis()
  const { config, settingsEnabled } = useAlignConfig()
  // The pitch the user has set (experimental settings) when it is configured, the default otherwise: the table turns
  // with the same value as the mesh-guided evaluation.
  const pitchUm =
    (turnsConfigured(config) ? (config.thread_pitch_mm as number) : (ALIGN_DEFAULTS.thread_pitch_mm ?? 0.7)) * 1000
  const library = plates.use().data
  const bed = basis.temp
  const shown = bed == null ? '' : String(bed)
  const blocked = bed == null
  const chain = guidedChain.use()
  // The chain after a new mesh is on its way: no other start meanwhile. Its note (stage 'hint') does not lock.
  const chainOn = chain.stage === 'mesh' || chain.stage === 'screws'
  useEffect(() => {
    if (chainOn && !meshTicked) setLevelMesh(true)
  }, [chainOn, meshTicked])
  const calJob = library?.calibration
  const calRunning = Boolean(calJob && calJob.stage !== 'off')
  // The hold time of step 1 (the same setting as in the Mesh tab): every start the user makes outside a session takes
  // it (lib/screws withSoak); the screw points the chain starts right after its mesh and the measurements of a session
  // do not.
  const soakText = calibrationForm.use().soak
  const soakMin = soakOk(soakText) ? Number(soakText) : 0
  // The 60 C default keeps the plain actions; another target is appended as ":<C>".
  const withBed = (action: string) => (bed === 60 ? action : `${action}:${bed}`)
  // A quick measurement (four corners or one corner): "2 x 3 probes" is the trailing ":2" after the explicit bed target.
  // The session start takes the same suffix: "2× 3 probes" is the trailing ":2" after the explicit bed target.
  const startAction = () => (probeTwice ? `screws:tram:start:${bed}:2` : withBed('screws:tram:start'))
  const measureAction = (corner?: ScrewId) => {
    const base = corner ? `screws:measure:corner:${corner}` : 'screws:measure'
    return probeTwice ? `${base}:${bed}:2` : withBed(base)
  }
  usePoll(refreshConsole, 2500) // the measurement result arrives in the console output
  usePoll(refreshScrewHistory, 2500) // the saved session trail arrives after each completed measurement
  const session = screwSession.use()
  const history = screwHistory.use()
  const consoleState = consoleStatus.use()
  const busy = consoleState.busy
  const job = screwJob.use()
  const [clockNow, setClockNow] = useState(() => performance.now())
  usePoll(() => setClockNow(performance.now()), 1000)
  const printerData = printer.use().data
  // The hold time before a measurement or a session start runs on the printer (it stands in the field at the top).
  const soak = screwSoakOf(printerData)
  const soaking = Boolean(soak)
  // The measurement behind a hold time starts when the hold ends: its job counts from then.
  const wasSoaking = useRef(soaking)
  useEffect(() => {
    if (wasSoaking.current && !soaking && screwJob.get().kind) screwJob.set({ at: performance.now() })
    wasSoaking.current = soaking
  }, [soaking])
  useEffect(() => {
    if (!job.kind) return
    if (busy && !job.sawBusy) screwJob.set({ sawBusy: true })
    else if (!busy && job.sawBusy) screwJob.set({ kind: null, corner: null, sawBusy: false })
    else if (!busy && !soaking && performance.now() - job.at > SCREW_JOB_FORGET_MS)
      screwJob.set({ kind: null, corner: null })
  }, [busy, job.kind, job.sawBusy, clockNow, soaking])
  const tram = printerData?.screws_session
  const printerSession = Boolean(tram?.active)
  const xyReady = Boolean(tram?.active && tram?.xy_homed)
  // X and Y still referenced (also without a session): a corner measured again homes only Z.
  const homedAxes = String(printerData?.motion?.homed_axes ?? '')
  const homedXY = homedAxes.includes('x') && homedAxes.includes('y')
  // A quick measurement is not possible during a session: the choice moves on to the session.
  useEffect(() => {
    if (printerSession && startMode === 'quick') pickMode('session')
  }, [printerSession, startMode])
  const shotText = screwText.use().text
  const quickState = screwQuick.use()
  const fullShot = screwValues(shotText)
  const merged =
    !fullShot && quickState.base && quickState.corner
      ? mergeCornerMeasure(quickState.base, shotText, quickState.corner)
      : null
  const oneShot = fullShot ?? merged?.values ?? null
  const sessionReady = SCREW_KEYS.every(k => Number.isFinite(session.heights[k]))
  const values = sessionReady ? session.heights : oneShot
  const unit = screwUnit.use().unit
  // With a mesh (step 1, both ways of measuring) the plate shape at each screw is taken off the four heights first: the
  // target of a screw is then the plate shape there, and the turn follows from what is left (lib/mesh-align
  // analyseScrewProbes). In a session the heights are the last four (a corner measured again replaces its own). The
  // mesh is a saved one, the temporary one or the printer's active mesh (basis.mesh), with the same calculation.
  const basisMesh = basis.mesh
  const meshOn = Boolean(basisMesh)
  const meshFit =
    basisMesh && values && SCREW_KEYS.every(k => Number.isFinite(values[k]))
      ? analyseScrewProbes(
          {
            FL: values[SCREW_KEYS[0]],
            FR: values[SCREW_KEYS[1]],
            RR: values[SCREW_KEYS[2]],
            RL: values[SCREW_KEYS[3]],
          },
          basisMesh,
          config
        )
      : null
  const meshAnalysis = basisMesh ? analyseMesh(basisMesh, config) : null
  // The mesh of the basis (saved, working or active mesh) carried along to the latest screw measurement: only for the
  // display, marked as calculated.
  const carried =
    meshFit && basisMesh && values
      ? carryMesh(
          basisMesh,
          {
            FL: values[SCREW_KEYS[0]],
            FR: values[SCREW_KEYS[1]],
            RR: values[SCREW_KEYS[2]],
            RL: values[SCREW_KEYS[3]],
          },
          config
        )
      : null
  const [alignView, setAlignView] = useState<'screws' | '3d' | '2d' | 'map'>('screws')
  const view3 = meshAnalysis ? alignView : 'screws'
  const nextView = () => setAlignView(v => (v === 'screws' ? '3d' : v === '3d' ? '2d' : v === '2d' ? 'map' : 'screws'))
  const planValues =
    values && meshFit ? Object.fromEntries(SCREW_KEYS.map((k, i) => [k, values[k] - meshFit.shape[i]])) : values
  // The strategy is the operator's choice (lib/screw-strategy): standard keeps front left as the fixed reference, the
  // optimized plan turns the front-left screw as well. Nothing here switches between them by itself.
  const strategyState = screwStrategy.use()
  const strategy = strategyState.strategy
  const planFull = planValues ? screwPlan(planValues, strategy) : null
  // Front left has moved since the last full measurement (a single measurement of that corner): with the optimized plan
  // every other corner's offset is stale, so no new plan is shown until all four corners are measured again.
  const running = history.supported ? history.sessions.find(s => s.kind !== 'quick' && s.ended == null) : undefined
  const moved = running ? referenceMove(running) : null
  const since = moved?.sinceFullUm
  const fourNeeded = strategy === 'optimized' && since != null && Math.abs(since) > 5
  const plan = fourNeeded ? null : planFull
  // With the optimized plan the front-left screw is turned next: the next measurement is a full four-corner one, and
  // single corners wait until all four are measured again (no half measurement against a moved reference).
  const fourStart = fourNeeded || Boolean(plan?.useOptimized && Math.abs(plan.shown[0]) > 0.02)
  // The change of each corner's offset to front left that the shown turns lead to (µm), 0 where no turn was given.
  const givenTurns = planFull
    ? planFull.shown.map((v, i) => (i > 0 && Math.abs(v) * 1000 > TURN_LIMIT_UM ? (v - planFull.shown[0]) * 1000 : 0))
    : []
  // Values of a quick measurement without a session: its corners can be measured again, quickly, one at a time.
  const quickMode = !printerSession && !sessionReady && oneShot != null
  const lang = i18n.use().lang
  const turn = screwTurn.use()
  // Turn amounts are shown for every plan, the combined one included: there the front-left screw has a row and a
  // turn of its own, and the note above the table says why.
  const turnShown = Boolean(plan)
  // The help under the table: bright text, first what the two modes show side by side, the two warnings that decide
  // whether a turn works at all in a frame, the click legend last.
  const [helpOpen, setHelpOpen] = useState(false)
  // A corner the page gave a turn for that hardly moved by the next measurement (lib/screws stuckCorners): the row of
  // that corner says that the sleeve may turn with the screw. Differences against front left, in micrometres; the
  // reference itself is left out.
  const cornerDiffs =
    values && SCREW_KEYS.every(k => Number.isFinite(values[k]))
      ? SCREW_KEYS.map(k => Math.round((values[k] - values[SCREW_KEYS[0]]) * 1000))
      : null
  // What was measured against front left, in millimetres: shown on its own, whichever strategy the plan follows.
  const offsets =
    values && SCREW_KEYS.every(k => Number.isFinite(values[k])) ? measuredOffsets(values) : SCREW_KEYS.map(() => 0)
  const measureSign =
    cornerDiffs && values ? `${cornerDiffs.join(',')}|${Math.round(values[SCREW_KEYS[0]] * 1000)}` : ''
  const lastDiffs = useRef<{ sign: string; diffs: number[]; turns: number[] } | null>(null)
  const [stuck, setStuck] = useState<number[]>([])
  useEffect(() => {
    if (!cornerDiffs) return
    const prev = lastDiffs.current
    if (prev?.sign === measureSign) return
    if (prev) setStuck(stuckCorners(prev.diffs, cornerDiffs, prev.turns))
    lastDiffs.current = { sign: measureSign, diffs: cornerDiffs, turns: givenTurns }
  }, [measureSign])
  // Back against front, mean of the two corners each: more than CARRIER_TILT_HINT_MM points at a bed carrier that
  // hangs askew on its Z axes rather than at the screws.
  const carrierTiltMm =
    values && SCREW_KEYS.every(k => Number.isFinite(values[k]))
      ? (values[SCREW_KEYS[2]] + values[SCREW_KEYS[3]]) / 2 - (values[SCREW_KEYS[0]] + values[SCREW_KEYS[1]]) / 2
      : null
  // The exact value that leads to zero, written in the chosen unit.
  // The clock in hours: "from 12 to 2:45", whole turns in front ("1 turn, then to 2:45"); exactly whole turns alone.
  // Against the clock (`ccw`, the direction the symbol beside it shows) the hand runs backwards: "from 12 to 10:30".
  const clockHoursText = (deg: number, ccw: boolean) => {
    const c = clockHours(deg, ccw)
    // Less than a quarter hour: no "from 12 to 12:00", which says nothing.
    if (!c.turns && c.onTwelve) return t('bed.turn_clock_small')
    if (!c.turns) return tpl('bed.turn_clock_to', { time: c.time })
    const turns = tpl(c.turns === 1 ? 'bed.turn_turns_one' : 'bed.turn_turns_other', { n: String(c.turns) })
    return c.onTwelve ? turns : tpl('bed.turn_clock_turns_to', { turns, time: c.time })
  }
  // The clock in minutes carries a sign beside the turn symbol: "+0:08" clockwise, "−0:08" against the clock.
  // The clock in minutes: signed minutes beside the turn symbol, "+3 min" clockwise, "−3 min" against the clock; whole
  // turns in words in front ("1 turn, −12 min"), whole turns alone; under half a minute "less than 1 min".
  const clockMinutesText = (deg: number, ccw: boolean) => {
    const minutes = Math.round(deg / 6) // 60 minutes = one turn
    if (!minutes) return t('bed.turn_clock_minutes_small')
    const turns = Math.floor(minutes / 60),
      rest = minutes % 60
    // "min" is the unit symbol, like µm and °C not translated.
    const signed = `${ccw ? '−' : '+'}${rest} min`
    if (!turns) return signed
    const words = tpl(turns === 1 ? 'bed.turn_turns_one' : 'bed.turn_turns_other', { n: String(turns) })
    return rest ? `${words}, ${signed}` : words
  }
  // The turn of a plan goes against the clock when it lowers the bed: the same rule as the symbol (d > 0: lower, ↺).
  const ccwOf = (x: TurnStage) => x.dir === 'lower'
  const turnValue = (x: TurnStage) => {
    if (turn.unit === 'clockhours') return clockHoursText(x.theta, ccwOf(x))
    if (turn.unit === 'clock') return clockMinutesText(x.theta, ccwOf(x))
    if (turn.unit === 'clicks') return clicksText(turnExact(x.theta, 'clicks', lang), 'other')
    if (turn.unit === 'fractions')
      return tpl('bed.turn_turns', { turns: turnExact(x.theta, 'fractions', lang), fraction: turnFraction(x.theta) })
    return turnExact(x.theta, turn.unit, lang)
  }
  // Exact mode: the exact value in the chosen unit, nothing else (the unit was chosen above).
  const turnExactText = (x: TurnStage) => turnValue(x)
  // Auto mode: what can really be turned, in the chosen unit: the angle rounded to whole clicks (5°), always with the
  // rest that is left in µm (so it is clear that this is the most exact setting, not exactly zero). Turns shows the
  // quarter turns plus clicks (a count of turns would give odd fractions). Over half a turn: a coarse step first.
  // Fractions in Auto: the turns as a decimal number first, the fraction in brackets behind ("0,25 turns (1/4)").
  const fractionText = (pick: { deg: number; text: string }) => {
    const n = turnsNumber(pick.deg, lang)
    if (!pick.text.includes('/')) return tpl(pick.deg === 360 ? 'bed.turn_turns_one' : 'bed.turn_turns_other', { n })
    return tpl('bed.turn_turns_fraction', { n, fraction: pick.text })
  }
  const turnAid = (x: TurnStage) => {
    const inUnit = (deg: number) =>
      turn.unit === 'clicks'
        ? clicksText(String(Math.round(deg / CLICK_DEG)), pluralOf(Math.round(deg / CLICK_DEG), lang))
        : turn.unit === 'clock'
          ? clockMinutesText(deg, ccwOf(x))
          : turn.unit === 'clockhours'
            ? clockHoursText(deg, ccwOf(x))
            : turn.unit === 'fractions'
              ? fractionText(turnFractionAid(deg))
              : `${deg}°`
    if (x.stage === 3) return tpl('bed.turn_coarse_first', { coarse: inUnit(x.coarse ?? 0) })
    if (!x.fine) return ''
    if (turn.unit === 'fractions') {
      // Simple fractions that can be set on the tool; the rest is what is left of the angle still to turn.
      const left = Math.abs(x.theta * (pitchUm / 360))
      const pick = turnFractionAid(left / (pitchUm / 360))
      const aidText = fractionText(pick)
      const rest = (x.dir === 'raise' ? 1 : -1) * (left - pick.deg * (pitchUm / 360))
      return tpl('bed.turn_aid', { aid: aidText, rest: heightLabel(rest / 1000, unit, true, lang) })
    }
    const { n } = x.fine
    const aid = inUnit(n * CLICK_DEG)
    return tpl('bed.turn_aid', { aid, rest: heightLabel(x.rest / 1000, unit, true, lang) })
  }
  const locked = pending || busy || chainOn || calRunning || soaking
  // The console works on a screw measurement (not on the steps of a mesh calibration, which have their own bar): only
  // then the tab says that a measurement runs.
  const screwBusy = busy && SCREW_JOB.test(consoleState.command) && !calRunning
  // An old mesh, or one without a date, can still be used: the note under the options says so and recommends a new mesh.
  const savedMeshAged = Boolean(basis.found && basis.age?.state !== 'ok')
  // With the tick but no mesh chosen (or none there yet) "Measure a new mesh" is the only start. The printer's active
  // mesh counts as one to choose.
  const anyMesh = Boolean(basis.active || (library?.available && library.plates.some(p => p.measures.length > 0)))
  const sessionBed = liveNumber(tram?.bed)
  // What was actually measured at the last result of the running session's newest measurement that has it.
  const lastIst = printerSession
    ? [
        ...(history.supported
          ? (history.sessions.find(s => s.kind !== 'quick' && s.ended == null)?.measurements ?? [])
          : []),
      ]
        .reverse()
        .find(m => m.ist)?.ist
    : undefined
  const tempRows: TempRow[] = printerSession
    ? [
        {
          id: 'bed',
          label: 'preheat.bed',
          target: sessionBed == null ? null : { value: sessionBed, approx: false },
          measured: lastIst?.bed,
          live: { value: liveNumber(printerData?.heater_bed?.temperature), digits: 1 },
        },
        {
          id: 'nozzle',
          label: 'common.nozzle',
          measured: lastIst?.nozzle,
          live: { value: liveNumber(printerData?.extruder?.temperature), digits: 1 },
        },
        {
          id: 'chamber',
          label: 'dashboard.chamber',
          measured: lastIst?.chamber,
          live: { value: liveNumber(printerData?.chamber?.temperature), digits: 0 },
        },
      ]
    : [
        {
          id: 'bed',
          label: 'preheat.bed',
          target: bed == null ? null : { value: bed, approx: false },
          live: { value: liveNumber(printerData?.heater_bed?.temperature), digits: 1 },
        },
      ]
  const rows = SCREW_CORNERS.map((c, i) => ({ ...c, i }))
  const plate = [rows[3], rows[2], rows[0], rows[1]]
  const statusKey: Key = screwBusy
    ? 'bed.tram_probing'
    : printerSession
      ? 'bed.tram_active'
      : planFull
        ? 'bed.measured_results'
        : 'bed.no_measurement'
  const jobSeconds = Math.max(0, Math.round((clockNow - job.at) / 1000))
  const jobStatus = job.kind
    ? tpl(
        job.kind === 'start'
          ? 'bed.tram_status_start'
          : job.kind === 'quick'
            ? 'bed.tram_status_quick'
            : 'bed.tram_status_probe',
        { n: jobSeconds, seconds: SCREW_SETTLE_S }
      )
    : tpl('bed.tram_status_unknown', { seconds: SCREW_SETTLE_S })
  // A bed clearly hotter than the target (as the program judges it: more than 2.5 K, see SCREWS_COOL_LIMIT_K): the start
  // says that cooling takes extra time. The note is live: it follows the bed and goes away once it no longer applies.
  const hotNote = () => {
    const live = liveNumber(printer.get().data?.heater_bed?.temperature)
    return bed != null && live != null && live > bed + BED_HOT_LIMIT_K
      ? `\n\n${tpl('bed.lvl_hot_warning', { live: formatLiveValue(live, lang, 1), temp: shown })}`
      : ''
  }
  const withHot = (text: string) => () => `${text}${hotNote()}`
  // The question of a start the user makes outside a session names the hold time of step 1 when there is one.
  const soakLine = () => (soakMin ? `\n\n${tpl('bed.calibration_soak', { temp: shown, minutes: soakMin })}` : '')
  const startQuestion = (text: string) => withHot(`${text}${soakLine()}`)
  const run = async (action: string, confirm?: ConfirmText) => {
    if (measuring.current) return false
    measuring.current = true
    setPending(true)
    try {
      if (confirm && !(await ask(confirm))) return false
      // The firmware unloads its active mesh during a screw measurement: with it as the basis, the page keeps a copy of it
      // from now (lib/level-state takeActiveCopy) and evaluates with that.
      if (basis.meshOn && basis.choice.kind === 'active') takeActiveCopy()
      if (!(await sendScrewAction(action, values, false))) return false
      openPage('bed', 'align')
      return true
    } catch (e) {
      notify(errText(e), 'error')
      return false
    } finally {
      measuring.current = false
      setPending(false)
    }
  }
  // The plate preselected in the question of a new mesh (working or final mesh): the one of the chosen mesh, else the
  // mounted one, else the first of the library.
  const meshPlate =
    basis.found?.plate ?? library?.plates.find(p => p.id === library?.current) ?? library?.plates[0] ?? null
  // Starts a mesh calibration with the temperature of step 1; `thenScrews`: the way of measuring of step 1 follows by
  // itself (the quick measurement or the session start), without a hold time. Without it, this is the final mesh.
  const startMesh = async (thenScrews: boolean) => {
    if (bed == null || measuring.current) return
    if (!tempOk(String(bed))) return notify(t('common.invalid_bed_temperature'), 'error')
    if (!meshPlate) return notify(t('bed.lvl_guided_no_plate'), 'error')
    // The plate's name is a choice in the question: the saved plates, the plate above preselected.
    const plates = library?.plates ?? [meshPlate]
    let target = meshPlate
    const [before, after = ''] = tpl(thenScrews ? 'bed.lvl_guided_confirm' : 'bed.lvl_final_confirm', {
      temp: shown,
      name: '\uE000',
    }).split(/[“”„"«»‘’‚']?\uE000[“”„"«»‘’‚']?/)
    // The hold time of step 1 (the same setting as in the Mesh tab) is named in the question with the temperature.
    const homing = homedXYZ(printerData?.motion?.homed_axes) ? '' : `\n\n${t('bed.calibration_homes_first')}`
    // The first mesh is only a temporary mesh: the question says so, and that a session starts after it.
    const temporary = thenScrews
      ? `\n\n${t('bed.lvl_work_temporary')}${startMode === 'session' ? `\n\n${t('bed.lvl_guided_then_session')}` : ''}`
      : ''
    // A mesh calibration ends an open screw session (the printer service does that by itself): the final mesh says so.
    const endsSession = printerSession ? `\n\n${t('bed.cal_ends_session')}` : ''
    const question = () => (
      <>
        {before}
        <Select
          class="mx-1 inline-block h-9 w-auto max-w-full align-baseline"
          aria-label={t('plates.mine_title')}
          data-level-confirm-plate
          value={target.id}
          onChange={e => {
            target = plates.find(p => p.id === e.currentTarget.value) ?? target
          }}
        >
          {plates.map(p => (
            <option key={p.id} value={p.id}>
              {p.name}
            </option>
          ))}
        </Select>
        {after}
        {soakLine()}
        {temporary}
        {endsSession}
        {homing}
        {hotNote()}
      </>
    )
    measuring.current = true
    setPending(true)
    try {
      if (!(await ask(question))) return
      // "Measure a new mesh" measures only the plate's temporary mesh; the final mesh replaces the stored measurement.
      await calibrate(target.side, bed, soakMin, library?.nozzle ?? '', target.id, thenScrews)
      // The chain remembers what was confirmed: the way of measuring follows with this bed target and these probes,
      // without a hold time (the bed is warm from the mesh).
      if (thenScrews)
        beginChain({
          plate: target.id,
          temp: bed,
          soak: soakMin,
          action: startMode === 'session' ? startAction() : measureAction(),
        })
      await refreshPlates()
      // The final mesh: on to the Mesh tab, where it is shown afterwards (its progress stands in the field at the top).
      if (!thenScrews) openPage('bed', 'mesh')
    } catch (e) {
      notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
    } finally {
      measuring.current = false
      setPending(false)
    }
  }
  // The four screw points with the bed target, probes and hold time of step 1, the same question: the quick start in
  // step 1 (with or without a mesh, which measure the same) and "Measure the screws again" in step 2.
  const measureScrews = () =>
    void run(
      withSoak(measureAction(), soakMin),
      startQuestion(tpl('bed.start_the_four_screw_load_cell', { temp: shown, seconds: SCREW_SETTLE_S }))
    )
  const startSession = () =>
    void run(
      withSoak(startAction(), soakMin),
      startQuestion(tpl('bed.tram_start_confirm', { seconds: SCREW_SETTLE_S, temp: shown }))
    )
  // The corner picked in the views or the table: shown only (cyan outline on the row and the mark), never saved.
  const [pickedCorner, setPickedCorner] = useState<ScrewId | null>(null)
  const pickCorner = (id: ScrewId) => setPickedCorner(p => (p === id ? null : id))
  const probeAction = (id: ScrewId) => `screws:tram:probe:${id}${probeTwice ? ':2' : ''}`
  const cornerName: Record<ScrewId, Key> = {
    FL: 'bed.corner_front_left',
    FR: 'bed.corner_front_right',
    RR: 'bed.corner_rear_right',
    RL: 'bed.corner_rear_left',
  }
  // How a corner of the shown values was measured. In a session: its newest measurement that has the corner (the saved trail, so
  // also after a reload); otherwise the console output of the measurement. null when it is not known.
  const readsOfConsole = useMemo(() => cornerReads(shotText), [shotText])
  const readSession = running ?? history.sessions.find(s => s.kind !== 'quick')
  const cornerRead = (id: ScrewId, key: string): { twice: boolean; probes: number[] | null } | null => {
    if (sessionReady) {
      const m = [...(readSession?.measurements ?? [])]
        .reverse()
        .find(x => x.kind !== 'quick' && typeof x.z[id] === 'number')
      return m ? { twice: m.twice, probes: m.probes?.[id] ?? null } : null
    }
    return readsOfConsole[key] ?? null
  }
  // Every deviation shown with the table follows the chosen height unit (µm or mm, the same decimals as the heights).
  const amount = since == null ? '' : heightLabel(since / 1000, unit, true, lang)
  const earlier = history.sessions.filter(s => s.kind !== 'quick' && s.ended != null)
  const quick = history.sessions.filter(s => s.kind === 'quick')
  const exportHistory = (kind: 'json' | 'csv' | 'excel') => {
    const payload = history.raw ?? { sessions: history.sessions }
    if (kind === 'json')
      saveText(
        'screw-levelling-history.json',
        `${JSON.stringify(payload, null, 2)}\n`,
        'application/json;charset=utf-8'
      )
    else if (kind === 'excel')
      saveText(
        'screw-levelling-history-excel.csv',
        screwHistoryCsv(history.sessions, 'excel'),
        'text/csv;charset=utf-8'
      )
    else saveText('screw-levelling-history.csv', screwHistoryCsv(history.sessions), 'text/csv;charset=utf-8')
  }
  const clearHistory = async () => {
    if (!(await ask(t('bed.tram_history_clear_confirm'), true))) return
    try {
      await post('/api/screws/history/clear')
      screwHistory.set({ supported: true, sessions: [], raw: { sessions: [] } })
    } catch (e) {
      notify(errText(e), 'error')
    }
  }
  const turnCount = plan
    ? rows.filter(c => !(!plan.useOptimized && c.i === 0) && Math.abs(plan.shown[c.i]) > 0.02).length
    : 0
  const meshWarnings: string[] = []
  // A plate bowed beyond the limit: the screws only set the tilt, so the page says that the shape stays (it never blocks).
  if (meshAnalysis?.shape_warning)
    meshWarnings.push(
      tpl('bed.lvl_warn_shape', { range: mm(meshAnalysis.residual_range, 2), limit: mm(config.shape_warn_mm, 2) })
    )
  if (meshFit && basis.found) {
    const { plate, measure } = basis.found
    const screwTemp = printerSession && sessionBed != null ? sessionBed : bed
    if (screwTemp != null && measure.temp !== screwTemp)
      meshWarnings.push(tpl('bed.lvl_warn_temp', { mesh: measure.temp, screws: screwTemp }))
    const mounted = library?.plates.find(p => p.id === library.current)
    if (library?.current && library.current !== plate.id)
      meshWarnings.push(tpl('bed.lvl_warn_plate', { mesh: plate.name, mounted: mounted?.name || '' }))
    if (library?.nozzle && measure.nozzle && library.nozzle !== measure.nozzle)
      meshWarnings.push(
        tpl('bed.lvl_warn_nozzle', {
          mesh: nozzleName(library, measure.nozzle),
          mounted: nozzleName(library, library.nozzle),
        })
      )
    if (meshFit.twist_warning)
      meshWarnings.push(tpl('bed.align_quick_twist', { twist: mm(meshFit.twist), limit: mm(QUICK_TWIST_WARN_MM, 2) }))
  }
  // After a working mesh the plate's stored mesh is older than it. Once the screws were turned since (judged from screw
  // measurements against the first one after the working mesh, never against a mesh), the stored mesh no longer fits
  // the bed until the final mesh is measured. Before that, the note of step 1 says that the working mesh is temporary.
  const workRef = levelWork.use().ref
  const workPlate = workRef ? library?.plates.find(p => p.id === workRef.plate) : undefined
  // While the next working mesh is measured, the warning about the one before is not shown.
  const workStale = Boolean(
    workRef &&
      workPlate &&
      chain.stage !== 'mesh' &&
      // The final mesh of this plate is being measured: the warning is done; it comes back if that run stops or fails.
      !(calRunning && calJob?.plate === workRef.plate && !calJob.work) &&
      !workDone(library, workRef) &&
      screwsTurnedSince(history.sessions, workRef.measured)
  )
  const step3 = fourNeeded
    ? t('bed.screw_four_needed_title')
    : plan
      ? turnCount
        ? tpl('bed.lvl_sum_turn', { n: turnCount })
        : t('bed.lvl_sum_ok')
      : t('bed.no_measurement')
  // With a mesh (quick or in a session): every corner is within the tolerance, so the page asks whether the final mesh is
  // to be measured now. The question stays as long as the latest measurement is within the tolerance and no mesh runs,
  // also after another page or a reload. It is not asked once the basis is a mesh measured after the newest screw
  // measurement (the final mesh), nor for a saved mesh after which no screw was turned (it still fits the bed; judged
  // as for the warning of the temporary mesh, lib/screws screwsTurnedSince). The temporary mesh always asks.
  const lastScrewTime = history.sessions.reduce((max, x) => Math.max(max, ...x.measurements.map(m => m.time)), 0)
  const basisMeasured = basis.found?.measure.measured ?? 0
  const finalDone = Boolean(
    basis.found &&
      ((lastScrewTime > 0 && basisMeasured >= lastScrewTime) ||
        (basis.choice.kind !== 'work' && !screwsTurnedSince(history.sessions, basisMeasured)))
  )
  // The printer's active mesh has no date, so it always asks, but only when there is a plate to keep the final mesh.
  const finalAsk =
    Boolean(plan) &&
    !turnCount &&
    meshOn &&
    Boolean(meshFit) &&
    !locked &&
    !finalDone &&
    (Boolean(basis.found) || Boolean(meshPlate))
  return (
    <Card id="screwFocus" class="flex-1">
      <CardHead
        icon="target"
        title="bed.level_title"
        sub="bed.nozzle_load_cell_measurement_at"
        end={<Tag tone={printerSession ? 'ok' : screwBusy ? 'info' : 'warning'}>{t(statusKey)}</Tag>}
      />
      <BasisStep
        basis={basis}
        locked={locked}
        head={
          // The way of measuring (quick or with a session) and "With mesh" right beside it, then the probes.
          <div class="mb-2 flex flex-wrap items-center gap-x-4 gap-y-1" data-level-start-head>
            <fieldset
              class="m-0 inline-flex min-w-0 max-w-full flex-wrap rounded-lg border border-edge p-0.5 text-xs"
              data-level-start-mode
            >
              <legend class="sr-only">{t('bed.lvl_mode_label')}</legend>
              {(['quick', 'session'] as const).map(m => (
                <button
                  key={m}
                  type="button"
                  aria-pressed={startMode === m}
                  data-level-start-mode-button={m}
                  disabled={m === 'quick' && printerSession}
                  title={m === 'quick' && printerSession ? t('bed.tram_one_shot_busy') : undefined}
                  class={cn(
                    'min-h-9 rounded-md px-3 font-semibold disabled:cursor-not-allowed disabled:opacity-40',
                    startMode === m ? 'bg-cyan text-ink' : 'text-muted hover:text-fg'
                  )}
                  onClick={() => setStartMode(m)}
                >
                  {t(m === 'quick' ? 'bed.lvl_mode_quick' : 'bed.lvl_mode_session')}
                </button>
              ))}
            </fieldset>
            <label class="flex min-h-9 items-center gap-1.5 text-xs font-semibold" data-level-mesh-toggle>
              <input
                type="checkbox"
                class="size-4 shrink-0 accent-cyan"
                checked={meshTicked}
                disabled={chainOn}
                aria-label={t('bed.lvl_mesh_toggle')}
                onChange={e => setLevelMesh(e.currentTarget.checked)}
              />
              <span class="whitespace-nowrap">{t('bed.lvl_mesh_toggle')}</span>
              <InfoHint text={t('bed.lvl_mesh_toggle_hint')} data-hint="mesh-toggle" />
            </label>
            <label class="flex min-h-9 items-center gap-1.5 text-xs text-muted" data-level-probe-twice>
              <input
                type="checkbox"
                class="size-4 shrink-0 accent-cyan"
                checked={probeTwice}
                disabled={locked}
                aria-label={t('bed.tram_probe_twice')}
                onChange={e => setProbeTwice(e.currentTarget.checked)}
              />
              <span class="whitespace-nowrap">{t('bed.tram_probe_twice')}</span>
              <InfoHint text={t('bed.tram_probe_twice_hint')} data-hint="probe-twice" />
            </label>
            {printerSession && (
              <small class="w-full text-xs text-muted" data-screw-quick-hint>
                {t('bed.tram_one_shot_busy')}
              </small>
            )}
          </div>
        }
      >
        {tempRows.length > 0 && (
          <div class="mt-2" id="screw-temp-values">
            <TempValues rows={tempRows} />
          </div>
        )}
        <div class="mt-3 grid gap-2" data-level-start>
          <div
            class={cn(
              'grid min-w-0 gap-2',
              // The start buttons have the same width in every case (26rem at most). With the tick "Measure a new mesh"
              // (or the note why there is no mesh) stands beside the start when both fit at that width, otherwise below.
              meshTicked
                ? 'grid-cols-[repeat(auto-fit,minmax(min(100%,26rem),26rem))] items-stretch justify-start'
                : 'grid-cols-[minmax(0,26rem)] items-start'
            )}
            data-screw-entries
          >
            {/* The start of the chosen way; with the tick only once a mesh is chosen, evaluated with it. */}
            {(!meshTicked || meshOn) && (
              <Button
                variant="primary"
                class={START_BUTTON}
                data-level-start-button={startMode}
                disabled={printerSession || locked || blocked}
                onClick={startMode === 'quick' ? measureScrews : startSession}
              >
                <Icon n="target" class="size-5" />
                <span>
                  <strong class="block">{t(startMode === 'quick' ? 'bed.measure_screws' : 'bed.tram_start')}</strong>
                  <Bullets
                    items={[
                      startMode === 'quick'
                        ? // "No wait" holds only without a hold time; with one the line names it (lib/screws withSoak).
                          soakMin
                          ? tpl('bed.lvl_entry_quick_soak', { minutes: soakMin })
                          : t('bed.lvl_entry_quick_short')
                        : tpl('bed.lvl_entry_session_short', {
                            seconds: SCREW_SETTLE_S,
                            minutes: SCREW_SESSION_TIMEOUT_MIN,
                          }),
                      ...(meshOn
                        ? [
                            t(
                              basis.choice.kind === 'work'
                                ? 'bed.lvl_work_short'
                                : basis.choice.kind === 'active'
                                  ? 'bed.lvl_active_short'
                                  : 'bed.lvl_saved_short'
                            ),
                          ]
                        : []),
                    ]}
                  />
                </span>
              </Button>
            )}
            {meshTicked && (
              <Button
                variant={meshOn ? 'default' : 'primary'}
                class={START_BUTTON}
                data-level-guided-new
                disabled={printerSession || locked || blocked}
                onClick={() => void startMesh(true)}
              >
                <Icon n="grid" class="size-5" />
                <span>
                  <strong class="block">{t('bed.lvl_guided_new')}</strong>
                  {/* What follows the mesh: the quick measurement (heaters off) or the session (the bed stays on). */}
                  <Bullets
                    items={[
                      startMode === 'quick'
                        ? t('bed.lvl_guided_short')
                        : tpl('bed.lvl_guided_short_session', { minutes: SCREW_SESSION_TIMEOUT_MIN }),
                    ]}
                  />
                </span>
              </Button>
            )}
            {meshTicked && !meshOn && (
              <p class="flex h-full min-h-11 items-center gap-2 px-1 text-xs text-muted" data-level-saved-mesh-off>
                <Icon n="info" class="size-4.5 shrink-0 text-cyan" />
                <span>{t(anyMesh ? 'bed.lvl_mesh_pick' : 'bed.lvl_saved_none')}</span>
              </p>
            )}
          </div>
          {/* An old mesh, or one without a date, can still be used; the note stands under the starts. */}
          {savedMeshAged && (
            <p class="flex max-w-[52rem] items-start gap-2 text-xs text-amber" role="status" data-level-saved-mesh-aged>
              <Icon n="info" class="mt-px size-4.5 shrink-0" />
              <span>
                {basis.age?.state === 'warn'
                  ? tpl('bed.lvl_saved_old', { days: basis.age.days ?? '–', prints: basis.age.prints ?? '–' })
                  : t('bed.lvl_saved_nodate')}
                {'. '}
                {t('bed.lvl_saved_recommend')}
              </span>
            </p>
          )}
        </div>
        {workStale && workPlate && (
          <div class="mt-2" data-level-work-stale>
            <Warn>{t('bed.lvl_work_stale')}</Warn>
          </div>
        )}
        {/* Outside a session the running measurement stands in the field at the top of the app (MeshStatus). */}
        {screwBusy && printerSession && (
          <p class="mt-3.5 text-xs text-amber" role="status" data-screw-status>
            {jobStatus}
          </p>
        )}
        {!busy && consoleState.cancelled && SCREW_JOB.test(consoleState.command) && (
          <div class="mt-3.5" data-screw-cancelled>
            <Notice icon="info">
              {t(
                !consoleState.command.includes('CC2_SCREW_TRAM')
                  ? 'bed.measure_cancelled'
                  : printerSession
                    ? 'bed.measure_cancelled_kept'
                    : 'bed.measure_cancelled_session'
              )}
            </Notice>
          </div>
        )}
      </BasisStep>
      <Step n={2} title={t('bed.lvl_s3')} summary={step3}>
        <div class="grid gap-3 cc2-sm:grid-cols-[.9fr_1.1fr]">
          <div class="relative min-w-0 self-start rounded-lg border border-edge p-3">
            {meshAnalysis && (
              <button
                type="button"
                class="absolute top-1.5 right-1.5 z-10 grid size-9 place-items-center rounded-md border border-cyan/70 bg-cyan/15 text-cyan hover:bg-cyan/25"
                title={t('bed.lvl_view_next')}
                aria-label={t('bed.lvl_view_next')}
                data-level-view-toggle
                onClick={nextView}
              >
                {view3 === 'screws' ? (
                  <Box size={18} />
                ) : view3 === '3d' ? (
                  <Layers size={18} />
                ) : view3 === '2d' ? (
                  <Grid3x3 size={18} />
                ) : (
                  <Grid2x2 size={18} />
                )}
              </button>
            )}
            {(view3 === '3d' || view3 === '2d') && meshAnalysis && basisMesh ? (
              <div data-level-mesh-3d>
                <AlignMesh3d
                  mesh={carried ?? basisMesh}
                  screws={config.screws}
                  mode={view3}
                  active={pickedCorner}
                  onPick={pickCorner}
                />
                {carried && (
                  <p class="mt-1 text-center text-xs font-semibold text-amber" data-level-carried>
                    {t('bed.lvl_carried')}
                  </p>
                )}
                <p class="mt-1 text-center text-xs text-muted">
                  {t(view3 === '3d' ? 'bed.lvl_view_3d_hint' : 'bed.lvl_view_2d_hint')}
                </p>
              </div>
            ) : view3 === 'map' && meshAnalysis ? (
              <div data-level-mesh-map>
                <ShapeMap
                  active={pickedCorner}
                  onPick={pickCorner}
                  analysis={meshAnalysis}
                  config={config}
                  reference={meshFit ? { n: 1, z: meshFit.shape[0] } : undefined}
                />
                <p class="mt-2 text-center text-xs">
                  {t('bed.align_shape_label')}: <b>{t(SHAPE[meshAnalysis.shape])}</b>
                </p>
                <p class="mt-0.5 text-center text-xs text-muted">
                  {tpl('bed.align_shape_range', {
                    range: mm(meshAnalysis.residual_range),
                    rms: mm(meshAnalysis.residual_rms),
                  })}
                </p>
              </div>
            ) : (
              <>
                <div class="relative grid min-h-80 grid-cols-2 rounded-lg border-2 border-muted">
                  <i class="absolute inset-y-2 left-1/2 border-l border-dashed border-edge" />
                  <i class="absolute inset-x-2 top-1/2 border-t border-dashed border-edge" />
                  {plate.map(c => {
                    const v = plan ? plan.shown[c.i] : 0,
                      tint = plan
                        ? `rgba(${v > 0 ? '20,207,233' : '255,123,134'},${(0.06 + Math.min(1, Math.abs(v) / 0.2) * 0.2).toFixed(2)})`
                        : undefined
                    return (
                      <div
                        key={c.id}
                        class={cn(
                          'relative flex flex-col items-center justify-center gap-1 rounded-lg p-2 text-xs',
                          pickedCorner === c.id && 'outline-2 -outline-offset-2 outline-cyan'
                        )}
                        style={{ background: tint }}
                      >
                        <NumMark
                          n={c.n}
                          picked={pickedCorner === c.id}
                          aria-label={`${c.n} · ${t(cornerName[c.id])}`}
                          data-screw-pick-mark={c.id}
                          onClick={() => pickCorner(c.id)}
                        />
                        <span class="px-1 text-center leading-tight">{t(cornerName[c.id])}</span>
                        <small class="text-xs text-muted">
                          X{c.x} Y{c.y}
                        </small>
                        {plan && <span class="font-bold">{heightLabel(v, unit, true, lang)}</span>}
                        <Button
                          class="min-h-11 px-3 text-xs"
                          disabled={(quickMode && c.id !== 'FL' ? locked || blocked : !xyReady || locked) || fourStart}
                          title={fourStart ? t('bed.screw_single_locked') : undefined}
                          aria-label={tpl('bed.tram_probe_corner', { corner: t(cornerName[c.id]) })}
                          data-screw-quick-corner={quickMode && c.id !== 'FL' ? c.id : undefined}
                          onClick={() =>
                            quickMode && c.id !== 'FL'
                              ? void run(
                                  withSoak(measureAction(c.id), soakMin),
                                  startQuestion(
                                    homedXY
                                      ? tpl('bed.tram_probe_quick_confirm_short', {
                                          temp: shown,
                                          seconds: SCREW_SETTLE_S,
                                        })
                                      : tpl('bed.tram_probe_quick_confirm', { temp: shown })
                                  )
                                )
                              : void run(probeAction(c.id))
                          }
                        >
                          {t(quickMode && c.id !== 'FL' ? 'bed.tram_probe_quick' : 'bed.tram_probe')}
                        </Button>
                        {busy && job.kind === 'probe' && job.corner === c.id && (
                          <small class="text-xs text-amber" data-screw-running={c.id}>
                            {t('bed.tram_measuring_corner')}
                          </small>
                        )}
                      </div>
                    )
                  })}
                </div>
                <div class="mt-1.5 text-center text-xs text-muted">{t('bed.top_view_front_edge_at_bottom')}</div>
                <div class="mt-1 flex items-center justify-center gap-1 text-center text-xs text-muted">
                  {t('bed.measure_order')}
                  {printerSession && <InfoHint text={t('bed.lvl_remeasure_info')} data-hint="remeasure" />}
                </div>
                <div class="mt-2 flex justify-between text-xs text-muted">
                  <span class="flex items-center gap-1">
                    <ArrowBigUp size={12} strokeWidth={1} /> {t('bed.axis_y_back')}
                  </span>
                  <span class="flex items-center gap-1">
                    <ArrowBigRight size={12} strokeWidth={1} /> {t('bed.axis_x_right')}
                  </span>
                </div>
              </>
            )}
          </div>
          <div>
            <div>
              {printerSession && (
                <div class="mb-2" data-screw-actions>
                  <Button
                    // With a measurement already made in the session, measuring again is the obvious next action.
                    variant={sessionReady || (running?.measurements.length ?? 0) > 0 ? 'primary' : 'default'}
                    class="min-h-11 px-3"
                    data-screw-check
                    disabled={!xyReady || locked}
                    onClick={() =>
                      void run(probeTwice ? 'screws:tram:check:2' : 'screws:tram:check', t('bed.tram_check_confirm'))
                    }
                  >
                    {t(fourStart ? 'bed.screw_four_start' : 'bed.tram_check')}
                  </Button>
                </div>
              )}
              <div class="mb-2 flex flex-wrap gap-1.5" data-screw-lists>
                <label class="flex min-w-[5.5rem] flex-[1_1_0] flex-col gap-0.5 text-xs text-muted" data-screw-mode>
                  {t('bed.turn_mode_label')}
                  <Select
                    class="w-full min-w-0 px-1 text-xs!"
                    value={turn.mode}
                    onChange={e => {
                      const next = parseTurnMode(e.currentTarget.value)
                      screwTurn.set({ mode: next })
                      ls.set(SCREW_TURN_MODE_KEY, next)
                    }}
                  >
                    {TURN_MODES.map(m => (
                      <option key={m} value={m}>
                        {t(TURN_MODE_LABEL[m])}
                      </option>
                    ))}
                  </Select>
                </label>
                <label class="flex min-w-[5rem] flex-[0.8_1_0] flex-col gap-0.5 text-xs text-muted" data-screw-unit>
                  {t('bed.tram_height_unit')}
                  <Select
                    class="w-full min-w-0 px-1 text-xs!"
                    value={unit}
                    onChange={e => {
                      const next = parseHeightUnit(e.currentTarget.value)
                      screwUnit.set({ unit: next })
                      ls.set(SCREW_UNIT_KEY, next)
                    }}
                  >
                    <option value="um">µm</option>
                    <option value="mm">mm</option>
                  </Select>
                </label>
                <div class="flex min-w-[8.5rem] flex-[1.3_1_0] items-end gap-1">
                  <label class="flex min-w-0 flex-1 flex-col gap-0.5 text-xs text-muted" data-screw-turn-unit>
                    <span class="flex h-4 items-center">{t('bed.turn_unit_label')}</span>
                    <Select
                      class="w-full min-w-0 px-1 text-xs!"
                      value={turn.unit}
                      onChange={e => {
                        const next = parseTurnUnit(e.currentTarget.value)
                        screwTurn.set({ unit: next })
                        ls.set(SCREW_TURN_UNIT_KEY, next)
                      }}
                    >
                      {TURN_UNITS.map(u => (
                        <option key={u} value={u}>
                          {t(TURN_UNIT_LABEL[u])}
                        </option>
                      ))}
                    </Select>
                  </label>
                  {/* Always reserved: the (i) for the click unit appears only then and moves nothing. */}
                  <span class="flex h-9 w-7 shrink-0 items-center justify-center" data-screw-hint-slot>
                    {turn.unit === 'clicks' && (
                      <InfoHint text={t('bed.turn_clicks_hint')} align="right" data-hint="clicks" />
                    )}
                  </span>
                </div>
              </div>
              <p class="mb-2 text-xs text-muted" data-screw-from-below>
                {t('bed.turn_from_below')}
              </p>
              {planFull && strategy === 'optimized' && (
                <div class="mb-2 flex flex-wrap items-center gap-2" data-screw-strategy-row>
                  <Button class="min-h-11 px-3 text-xs" data-screw-strategy-back onClick={returnToStandard}>
                    {t('bed.screw_opt_back')}
                  </Button>
                </div>
              )}
              {plan && strategy === 'standard' && plan.canOptimize && !strategyState.kept && (
                <div
                  class="mb-2 grid gap-2 rounded-md border border-amber bg-amber/10 p-2.5 text-xs"
                  role="status"
                  data-screw-optimize-suggest
                >
                  <p class="flex items-center gap-1.5 text-sm font-semibold text-amber">
                    <Icon n="light" class="size-4.5 shrink-0" />
                    {t('bed.screw_opt_title')}
                  </p>
                  <p class="text-fg">
                    {tpl('bed.optimized_reference_adjustment', {
                      word: t(plan.optimized[0] > 0 ? 'bed.lower_2' : 'bed.raise_2'),
                      amount: Math.abs(Math.round(plan.optimized[0] * 1000)),
                      before: plan.before,
                      best: plan.best,
                    })}
                  </p>
                  <p class="text-muted">{t('bed.screw_opt_note')}</p>
                  <div class="flex flex-wrap gap-2">
                    <Button
                      variant="primary"
                      class="min-h-11 px-3 text-xs"
                      data-screw-optimize-choose
                      onClick={chooseOptimized}
                    >
                      <Icon n="bolt" class="size-4 shrink-0" />
                      {t('bed.screw_opt_choose')}
                    </Button>
                    <Button class="min-h-11 px-3 text-xs" data-screw-optimize-keep onClick={keepStandard}>
                      {t('bed.screw_opt_keep')}
                    </Button>
                  </div>
                </div>
              )}
              {plan?.useOptimized && (
                <div class="mb-2" data-screw-combined-note>
                  <Notice icon="info">
                    {tpl('bed.optimized_reference_adjustment', {
                      word: t(plan.shown[0] > 0 ? 'bed.lower_2' : 'bed.raise_2'),
                      amount: Math.abs(Math.round(plan.shown[0] * 1000)),
                      before: plan.before,
                      best: plan.best,
                    })}
                  </Notice>
                </div>
              )}
              {fourNeeded && (
                <div
                  class="mb-2 grid gap-1 rounded-md border border-amber bg-amber/10 p-2.5 text-xs"
                  role="status"
                  data-screw-four-needed
                >
                  <div class="flex flex-wrap items-center justify-between gap-x-3 gap-y-1.5">
                    <p class="flex items-center gap-1.5 text-sm font-semibold text-amber">
                      <Icon n="info" class="size-4.5 shrink-0" />
                      {t('bed.screw_four_needed_title')}
                    </p>
                    {/* The suggestion block (with its strategy label) is hidden now: the label stands here, next to the way back. */}
                    <Tag tone="warning" data-screw-strategy="optimized">
                      <Icon n="bolt" class="size-3.5 shrink-0" />
                      {t('bed.screw_strategy_optimized')}
                    </Tag>
                  </div>
                  <p class="text-fg">{tpl('bed.screw_four_needed_text', { amount })}</p>
                </div>
              )}
              <section
                class={cn('mb-2 rounded-lg border border-edge bg-field/40 p-2.5', fourNeeded && 'opacity-60')}
                data-screw-measured-block
              >
                <div class="mb-2 flex flex-wrap items-baseline justify-between gap-x-3 gap-y-0.5">
                  <h4 class="flex items-center gap-1.5 text-sm font-semibold">
                    <ChartColumn size={16} strokeWidth={1.75} aria-hidden="true" class="shrink-0" />
                    {t('bed.screw_measured_title')}
                    {meshFit && <InfoHint text={t('bed.lvl_shape_info')} data-hint="shape" subtle />}
                  </h4>
                  <p class="text-xs text-muted" data-screw-block-note>
                    {t('bed.screw_measured_note')}
                  </p>
                </div>
                <div class="overflow-x-auto rounded-md border border-edge bg-panel">
                  <table class="w-full border-collapse text-xs max-sm:block" data-screw-measured-table>
                    <thead class="max-sm:hidden">
                      <tr class="text-left text-xs text-muted">
                        <th class="w-[26%] p-2 font-normal">{t('common.position')}</th>
                        <th class="p-2 font-normal">{t('bed.screw_col_z')}</th>
                        <th class="p-2 font-normal">{t('bed.screw_col_offset')}</th>
                        <th class="w-[22%] p-2 font-normal">{t('bed.screw_col_status')}</th>
                      </tr>
                    </thead>
                    <tbody class="max-sm:block">
                      {rows.map(c => {
                        const known = Boolean(values && Number.isFinite(values[c.key]))
                        const read = cornerRead(c.id, c.key)
                        // "Deviation" (with a small (i) naming the single probes) only when the probes of this corner in this
                        // measurement spread by more than PROBE_SPREAD_LIMIT_UM; no comparison with an earlier measurement, and
                        // without single probes nothing.
                        const spread = read?.probes && read.probes.length > 1 ? probeSpreadUm(read.probes) : 0
                        const deviation =
                          read?.probes && spread > PROBE_SPREAD_LIMIT_UM
                            ? tpl('bed.lvl_dev_probes', {
                                values: read.probes.map(v => heightLabel(v, unit, false, lang)).join(' · '),
                                span: heightLabel(spread / 1000, unit, false, lang),
                              })
                            : ''
                        const picked = pickedCorner === c.id
                        return (
                          <tr
                            key={c.id}
                            class={cn(
                              'cursor-pointer border-t border-edge max-sm:grid max-sm:min-h-11 max-sm:grid-cols-[repeat(auto-fit,minmax(7rem,1fr))] max-sm:gap-x-2 max-sm:px-2 max-sm:py-1.5',
                              // Only the reference row carries a quiet tint, and only once something was measured; what is
                              // framed is what the operator clicked.
                              c.i === 0 && known && 'bg-cyan/10',
                              picked && 'outline-2 -outline-offset-2 outline-cyan'
                            )}
                            data-screw-measured-row={c.id}
                            data-picked={picked ? '' : undefined}
                            onClick={e => {
                              if (!(e.target as Element).closest('button,a,input,select,summary,[role="button"]'))
                                pickCorner(c.id)
                            }}
                          >
                            <td class="p-2 align-middle font-semibold break-words max-sm:col-span-full max-sm:p-0">
                              <span class="inline-flex items-center gap-2">
                                <NumMark n={c.n} picked={picked} />
                                {t(cornerName[c.id])}
                              </span>
                            </td>
                            <td class="p-2 align-middle whitespace-nowrap max-sm:col-span-1 max-sm:p-0 max-sm:pt-1 max-sm:whitespace-normal">
                              <span class="hidden text-muted max-sm:block">{t('bed.screw_col_z')}</span>
                              <span class="whitespace-nowrap" data-screw-zheight={c.id}>
                                {known && values ? heightLabel(values[c.key], unit, false, lang) : '—'}
                              </span>
                            </td>
                            <td class="p-2 align-middle max-sm:col-span-1 max-sm:p-0 max-sm:pt-1">
                              <span class="hidden text-muted max-sm:block">{t('bed.screw_col_offset')}</span>
                              <strong class="font-semibold whitespace-nowrap" data-screw-offset={c.id}>
                                {known && values ? heightLabel(offsets[c.i], unit, true, lang) : '—'}
                              </strong>
                              {/* The measured fact against front left (negative = lies lower), independent of the plan. */}
                              {known && Math.round(Math.abs(offsets[c.i]) * 1000) !== 0 && (
                                <>
                                  {' '}
                                  <span class="text-muted" data-screw-lies={offsets[c.i] < 0 ? 'low' : 'high'}>
                                    ({t(offsets[c.i] < 0 ? 'bed.screw_lies_low' : 'bed.screw_lies_high')})
                                  </span>
                                </>
                              )}
                              {meshFit && (
                                <small class="block text-xs text-muted">
                                  {t('bed.lvl_col_shape')}:{' '}
                                  <span class="whitespace-nowrap" data-screw-shape={c.id}>
                                    {heightLabel(meshFit.shape[c.i] - meshFit.shape[0], unit, true, lang)}
                                  </span>
                                </small>
                              )}
                            </td>
                            <td class="p-2 align-middle max-sm:col-span-full max-sm:p-0 max-sm:pt-1">
                              <span class="hidden text-muted max-sm:block">{t('bed.screw_col_status')}</span>
                              {c.i === 0 && known && (
                                <span
                                  class="inline-block rounded-md bg-cyan px-1.5 font-semibold leading-4 text-ink sm:whitespace-nowrap"
                                  data-screw-measured={c.id}
                                >
                                  {t('bed.lvl_ref_abs')}
                                </span>
                              )}
                              {/* The status is the verdict of the plan for this corner, the same one the suggestion shows, in the
                                  same colour: no badge, no frame, only the symbol and the word. */}
                              {c.i !== 0 && known && plan && (
                                <StatusMark verdict={screwVerdict(plan.shown[c.i])} corner={c.id} />
                              )}
                              {deviation && (
                                <span class="flex items-center gap-0.5 text-muted" data-screw-deviation={c.id}>
                                  {t('bed.lvl_deviation')}
                                  <InfoHint subtle text={deviation} data-hint={`deviation-${c.id}`} />
                                </span>
                              )}
                              {!(c.i === 0 && known) && !(known && plan) && !deviation && (
                                <span class="text-muted">—</span>
                              )}
                            </td>
                          </tr>
                        )
                      })}
                    </tbody>
                  </table>
                </div>
                {/* Only with the optimized plan, once, under the table: the status follows that plan, which also moves front left. */}
                {strategy === 'optimized' && plan && (
                  <p class="mt-2 flex items-start gap-1.5 text-xs text-muted" data-screw-plan-note>
                    <Icon n="info" class="mt-px size-3.5 shrink-0" />
                    <span>{t('bed.screw_status_plan_note')}</span>
                  </p>
                )}
              </section>
              {meshFit && meshAnalysis && (
                <div class="mb-2 grid grid-cols-1 gap-2 cc2-sm:grid-cols-2" data-level-mesh-tiles>
                  <Tile
                    name="tilt-x"
                    label={t('bed.align_tilt_x')}
                    value={`${signedMm(meshFit.tilt_x)} mm`}
                    sub={`${tiltWords(meshFit.tilt_x, 'bed.align_right_high', 'bed.align_left_high')} · ${tpl('bed.align_over_span', { span: meshAnalysis.span })}`}
                  />
                  <Tile
                    name="tilt-y"
                    label={t('bed.align_tilt_y')}
                    value={`${signedMm(meshFit.tilt_y)} mm`}
                    sub={`${tiltWords(meshFit.tilt_y, 'bed.align_back_high', 'bed.align_front_high')} · ${tpl('bed.align_over_span', { span: meshAnalysis.span })}`}
                  />
                </div>
              )}
              {!fourNeeded && (
                <>
                  <section class="rounded-lg border border-edge bg-field/40 p-2.5" data-screw-proposal-block>
                    <div class="mb-2 flex flex-wrap items-baseline justify-between gap-x-3 gap-y-1">
                      <h4 class="flex items-center gap-1.5 text-sm font-semibold">
                        <Wrench size={16} strokeWidth={1.75} aria-hidden="true" class="shrink-0" />
                        {t('bed.screw_proposal_title')}
                      </h4>
                      {/* The note and the strategy label share the right side of the head; on a phone the label stands under the note. */}
                      <div
                        class="flex flex-wrap items-center justify-end gap-x-3 gap-y-1 max-sm:basis-full max-sm:flex-col max-sm:items-start"
                        data-screw-proposal-aside
                      >
                        <p class="text-xs text-muted" data-screw-block-note>
                          {t('bed.screw_proposal_note')}
                        </p>
                        {planFull && (
                          <Tag tone={strategy === 'optimized' ? 'warning' : 'info'} data-screw-strategy={strategy}>
                            <Icon n={strategy === 'optimized' ? 'bolt' : 'target'} class="size-3.5 shrink-0" />
                            {t(
                              strategy === 'optimized' ? 'bed.screw_strategy_optimized' : 'bed.screw_strategy_standard'
                            )}
                          </Tag>
                        )}
                      </div>
                    </div>
                    <div class="overflow-x-auto rounded-md border border-edge bg-panel">
                      <table class="w-full border-collapse text-xs max-sm:block" data-screw-proposal-table>
                        <thead class="max-sm:hidden">
                          <tr class="text-left text-xs text-muted">
                            <th class="w-[22%] p-2 font-normal">{t('common.position')}</th>
                            <th class="p-2 font-normal">{t('bed.screw_col_target')}</th>
                            <th class="p-2 font-normal">{t('bed.screw_col_direction')}</th>
                            <th class="w-[16%] p-2 font-normal">{t('bed.screw_col_turn')}</th>
                          </tr>
                        </thead>
                        <tbody class="max-sm:block">
                          {rows.map(c => {
                            const d = plan?.shown[c.i] ?? 0,
                              verdict = screwVerdict(d),
                              within = verdict === 'ok'
                            const read = cornerRead(c.id, c.key)
                            // "±x" behind the direction when the amount lies within x of the limit of the turn instruction (20 µm).
                            const limitUm = Math.round(Math.abs(d) * 1000)
                            const tolerance = read ? (read.twice ? TOLERANCE_UM.twice : TOLERANCE_UM.once) : null
                            const nearLimit =
                              c.i !== 0 && plan && tolerance != null && Math.abs(limitUm - TURN_LIMIT_UM) <= tolerance
                            const leave = c.i === 0 && !plan?.useOptimized
                            // The turn the row asks for: none for the reference that stays, none within the tolerance.
                            const turning = Boolean(plan) && !leave && !within
                            const target = !plan
                              ? null
                              : leave
                                ? 'bed.screw_leave_unchanged'
                                : plan.useOptimized
                                  ? c.i === 0
                                    ? 'bed.screw_target_fl_moved'
                                    : 'bed.screw_target_front_left_moved'
                                  : 'bed.screw_target_front_left'
                            // The first line of every cell sits on the middle of the 28 px number mark of its row (8 px padding + 14 px = 22 px),
                            // so a dash stands exactly where the name stands.
                            const cell = 'p-2 pt-3.5 align-top break-words max-sm:col-span-full max-sm:p-0 max-sm:pt-1'
                            return (
                              <tr
                                key={c.id}
                                class={cn(
                                  'cursor-pointer border-t border-edge max-sm:grid max-sm:min-h-11 max-sm:grid-cols-[repeat(auto-fit,minmax(7rem,1fr))] max-sm:gap-x-2 max-sm:px-2 max-sm:py-1.5',
                                  plan?.useOptimized && c.i === 0 && 'bg-amber/10',
                                  pickedCorner === c.id && 'outline-2 -outline-offset-2 outline-cyan'
                                )}
                                data-screw-row={c.id}
                                data-picked={pickedCorner === c.id ? '' : undefined}
                                // A click on the row picks its corner (display only); buttons and hints in it keep their own action.
                                onClick={e => {
                                  if (!(e.target as Element).closest('button,a,input,select,summary,[role="button"]'))
                                    pickCorner(c.id)
                                }}
                              >
                                <td class="p-2 align-top font-semibold break-words max-sm:col-span-full max-sm:p-0">
                                  <span class="inline-flex items-center gap-2">
                                    <NumMark
                                      n={c.n}
                                      picked={pickedCorner === c.id}
                                      aria-label={`${c.n} · ${t(cornerName[c.id])}`}
                                      data-screw-pick-row={c.id}
                                      onClick={() => pickCorner(c.id)}
                                    />
                                    {t(cornerName[c.id])}
                                  </span>
                                </td>
                                <td class={cell} data-screw-goal={c.id}>
                                  <span class="hidden text-muted max-sm:block">{t('bed.screw_col_target')}</span>
                                  {target ? t(target) : '—'}
                                </td>
                                <td class={cell} data-screw-adjust={c.id}>
                                  <span class="hidden text-muted max-sm:block">{t('bed.screw_col_direction')}</span>
                                  {!plan ? (
                                    '—'
                                  ) : leave ? (
                                    <span class="break-words text-muted italic" data-screw-leave>
                                      {t('bed.screw_no_turn')}
                                    </span>
                                  ) : within ? (
                                    <>
                                      <span
                                        class={cn('flex items-center gap-1 font-semibold', SCREW_VERDICT_TONE.ok)}
                                        data-screw-ok
                                      >
                                        <Check size={14} strokeWidth={2} aria-hidden="true" class="shrink-0" />
                                        <span>{t('bed.lvl_in_order')}</span>
                                      </span>
                                      {/* The user decides: within the tolerance the small adjustment that is still possible stays visible. */}
                                      {limitUm >= 1 ? (
                                        <small
                                          class="mt-0.5 block text-xs break-words text-muted"
                                          data-screw-possible={c.id}
                                        >
                                          {tpl('bed.lvl_possible', {
                                            word: t(d > 0 ? 'bed.turn_lower' : 'bed.turn_raise'),
                                            amount: heightLabel(Math.abs(d), unit, false, lang),
                                          })}
                                          {' · '}
                                          <span title={t(d > 0 ? 'bed.turn_ccw' : 'bed.turn_cw')}>
                                            {d > 0 ? '↺' : '↻'} {turnExactText(turnPlan(-d * 1000, pitchUm))}
                                          </span>
                                        </small>
                                      ) : (
                                        <small
                                          class="mt-0.5 block text-xs break-words text-muted"
                                          data-screw-no-turn-needed
                                        >
                                          {t('bed.screw_no_turn_needed')}
                                        </small>
                                      )}
                                    </>
                                  ) : (
                                    <span
                                      class={cn('flex items-center gap-1 font-semibold', SCREW_VERDICT_TONE[verdict])}
                                      data-screw-direction={verdict === 'lower' ? 'lower' : 'raise'}
                                    >
                                      {verdict === 'lower' ? (
                                        <ArrowDown size={14} strokeWidth={2} aria-hidden="true" class="shrink-0" />
                                      ) : (
                                        <ArrowUp size={14} strokeWidth={2} aria-hidden="true" class="shrink-0" />
                                      )}
                                      <span>
                                        {t(verdict === 'lower' ? 'bed.turn_lower' : 'bed.turn_raise')}{' '}
                                        <span data-screw-amount={c.id}>
                                          {heightLabel(Math.abs(d), unit, false, lang)}
                                        </span>
                                      </span>
                                    </span>
                                  )}
                                  {nearLimit && tolerance != null && (
                                    <small
                                      class="mt-0.5 block text-xs font-normal text-muted"
                                      data-screw-tolerance={c.id}
                                    >
                                      ±{heightLabel(tolerance / 1000, unit, false, lang)}
                                    </small>
                                  )}
                                  {/* A turn was given here, and the next measurement shows that the corner hardly moved. */}
                                  {stuck.includes(c.i) && (
                                    <span
                                      class="mt-1 flex items-start gap-1 rounded-md border border-amber bg-amber/10 px-1.5 py-1 text-xs font-semibold break-words text-amber"
                                      role="status"
                                      data-screw-stuck={c.id}
                                    >
                                      <TriangleAlert size={14} strokeWidth={1.75} class="mt-px shrink-0" />
                                      <span>{t('bed.lvl_stuck')}</span>
                                    </span>
                                  )}
                                </td>
                                <td class={cn(cell, 'text-muted')} data-screw-turn-cell={c.id}>
                                  <span class="hidden text-muted max-sm:block">{t('bed.screw_col_turn')}</span>
                                  {turning && turnShown ? (
                                    <span class="flex flex-wrap items-center gap-x-1.5 text-fg">
                                      {d > 0 ? (
                                        <RotateCcw
                                          size={13}
                                          strokeWidth={1.75}
                                          class="shrink-0"
                                          aria-label={t('bed.turn_ccw')}
                                          role="img"
                                          data-screw-rotation="ccw"
                                        />
                                      ) : (
                                        <RotateCw
                                          size={13}
                                          strokeWidth={1.75}
                                          class="shrink-0"
                                          aria-label={t('bed.turn_cw')}
                                          role="img"
                                          data-screw-rotation="cw"
                                        />
                                      )}
                                      {turn.mode === 'exact' ? (
                                        <span data-screw-exact={c.id}>
                                          {turnExactText(turnPlan(-d * 1000, pitchUm))}
                                        </span>
                                      ) : (
                                        <span data-screw-aid={c.id}>{turnAid(turnPlan(-d * 1000, pitchUm))}</span>
                                      )}
                                    </span>
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
                  </section>
                  {/* A short reason for the two separate blocks. */}
                  <p class="mt-2 flex items-start gap-1.5 text-xs text-muted" data-screw-blocks-note>
                    <Lightbulb size={14} strokeWidth={1.75} aria-hidden="true" class="mt-px shrink-0" />
                    <span>{t('bed.screw_blocks_note')}</span>
                  </p>
                </>
              )}
              {quickMode && !homedXY && (
                <p class="mt-2 text-xs text-muted" data-screw-quick-corner-hint>
                  {t('bed.lvl_corner_full_hint')}
                </p>
              )}
              {/* After turning: measure again where the evaluation stands (a session has its own buttons in the table). */}
              {planFull && !printerSession && (
                <Button
                  variant="primary"
                  class="mt-2 min-h-11 px-4"
                  data-level-measure-again
                  disabled={locked || blocked}
                  onClick={measureScrews}
                >
                  <Icon n="target" class="size-5" />
                  {t(fourStart ? 'bed.screw_four_start' : 'bed.lvl_measure_again')}
                </Button>
              )}
              {/* The help and "Create new mesh" share one row. Opened, the title is the tab of one frame whose text takes the
                  full width below that row (a subgrid draws the frame's top edge under the button), so nothing moves. */}
              <div class="mt-2 grid grid-cols-[minmax(0,1fr)_auto] items-start gap-x-2" data-screw-help-row>
                <details
                  class={cn('min-w-0 rounded-md border border-edge px-3', helpOpen && 'rounded-b-none border-b-0 pb-2')}
                  data-screw-help
                  open={helpOpen}
                  onToggle={e => setHelpOpen(e.currentTarget.open)}
                >
                  <summary class="flex min-h-9 cursor-pointer items-center text-sm font-semibold">
                    {t('bed.tram_help_title')}
                  </summary>
                </details>
                <Button
                  class="h-auto min-h-9 max-w-full shrink-0 px-3 py-1 text-xs whitespace-normal"
                  data-level-new-mesh-open
                  disabled={printerSession}
                  onClick={() => {
                    // To the Mesh tab with the plate side and the temperature of step 1 preselected.
                    calibrationForm.set({
                      temp: shown,
                      side: basis.found?.plate.side === 'B' ? 'default1' : 'default',
                      target: null,
                    })
                    openPage('bed', 'mesh')
                  }}
                >
                  {t('bed.lvl_new_mesh')}
                </Button>
                {helpOpen && (
                  <div class="col-span-2 grid grid-cols-subgrid" data-screw-help-body>
                    <span class="h-2 border-l border-edge" />
                    <span class="-ml-2 h-2 rounded-tr-md border-t border-r border-edge" />
                    <div class="col-span-2 grid gap-2 rounded-b-md border border-t-0 border-edge px-3 pb-3 text-xs text-fg">
                      <div class="grid gap-x-6 gap-y-2 cc2-sm:grid-cols-2" data-screw-help-modes>
                        {(['auto', 'exact'] as const).map(m => (
                          <p key={m} data-screw-help-mode={m}>
                            <strong class="block text-sm font-semibold">{t(TURN_MODE_LABEL[m])}</strong>
                            {t(m === 'auto' ? 'bed.help_mode_auto' : 'bed.help_mode_exact')}
                          </p>
                        ))}
                      </div>
                      <p>{t(plan?.useOptimized ? 'bed.front_left_reference_optimized' : 'bed.front_left_reference')}</p>
                      <p data-screw-help-tolerance>{t('bed.help_tolerance')}</p>
                      <p>{t('bed.mesh_capture_stays_separate')}</p>
                      <p data-screw-turn-legend>{t('bed.turn_legend')}</p>
                      <div
                        class="grid gap-1.5 rounded-md border border-amber bg-amber/10 px-2.5 py-2"
                        data-screw-help-warn
                      >
                        <p data-screw-help-sleeve>{t('bed.help_sleeve')}</p>
                        <p data-screw-help-seated>{t('bed.help_not_bottomed')}</p>
                        <p data-screw-help-rounds>{t('bed.help_rounds')}</p>
                      </div>
                    </div>
                  </div>
                )}
              </div>
            </div>
          </div>
        </div>
        {finalAsk && (
          <div
            class="mt-3 flex flex-wrap items-center gap-x-3 gap-y-2 rounded-md border border-cyan bg-cyan/10 px-3 py-2"
            role="status"
            data-level-final-ask
          >
            <strong class="min-w-[min(100%,14rem)] flex-1 text-sm font-semibold">{t('bed.lvl_final_ask')}</strong>
            <Button
              variant="primary"
              class="min-h-9 px-3 text-xs"
              data-level-final-yes
              onClick={() => void startMesh(false)}
            >
              {t('bed.lvl_final_yes')}
            </Button>
          </div>
        )}
        {meshOn && values && !meshFit && (
          <Notice icon="info">
            <span data-level-mesh-unusable>{t('bed.lvl_mesh_not_usable')}</span>
          </Notice>
        )}
        {meshWarnings.map(text => (
          <div key={text} data-level-mesh-warning>
            <Warn>{text}</Warn>
          </div>
        ))}
        {carrierTiltMm != null && Math.abs(carrierTiltMm) > CARRIER_TILT_HINT_MM && (
          <Notice icon="info">
            <span data-screw-carrier-tilt>
              {tpl('bed.lvl_carrier_tilt', {
                amount: heightLabel(Math.abs(carrierTiltMm), unit, false, lang),
                side: t(carrierTiltMm > 0 ? 'bed.align_back_high' : 'bed.align_front_high'),
              })}
            </span>
          </Notice>
        )}
        {quickMode && merged && Math.abs(Math.round(merged.shift * 1000)) > 10 && (
          <Notice icon="info">
            <span data-screw-quick-shift>
              {tpl('bed.tram_quick_ref_shift', { amount: heightLabel(merged.shift, unit, true, lang) })}
            </span>
          </Notice>
        )}
        {since != null && Math.abs(since) <= 5 && (
          <p class="mt-3.5 text-xs text-muted">{tpl('bed.tram_reference_stable', { amount })}</p>
        )}
        {since != null && Math.abs(since) > 5 && !fourNeeded && (
          <Notice icon="info">
            <span>{tpl('bed.tram_reference_stale', { amount })}</span>
          </Notice>
        )}
      </Step>
      {history.supported && (
        <div id="screw-history" class="mt-3.5">
          <details class="rounded-md border border-edge px-3">
            <summary class="flex min-h-11 cursor-pointer items-center text-base font-semibold">
              {t('bed.tram_history')}
            </summary>
            {earlier.length === 0 ? (
              <p class="pb-3 text-xs text-muted">{t('bed.tram_history_empty')}</p>
            ) : (
              <ScrewHistoryTable entries={earlier} variant="sessions" unit={unit} lang={lang} />
            )}
            <div class="mt-3.5 border-t border-edge pt-3">
              <h3 class="text-base font-semibold">{t('bed.tram_quick_history')}</h3>
              {quick.length === 0 ? (
                <p class="py-2 text-xs text-muted">{t('bed.tram_quick_empty')}</p>
              ) : (
                <ScrewHistoryTable entries={quick} variant="quick" unit={unit} lang={lang} />
              )}
            </div>
            <div class="flex flex-wrap gap-2 border-t border-edge py-3">
              <Button class="min-h-11" onClick={() => exportHistory('json')}>
                {t('bed.tram_history_export_json')}
              </Button>
              <Button class="min-h-11" onClick={() => exportHistory('csv')}>
                {t('bed.tram_history_export_csv')}
              </Button>
              <Button class="min-h-11" onClick={() => exportHistory('excel')}>
                {t('bed.tram_history_export_csv_excel')}
              </Button>
              <Button class="min-h-11" variant="danger" disabled={locked} onClick={() => void clearHistory()}>
                {t('bed.tram_history_clear')}
              </Button>
            </div>
          </details>
        </div>
      )}
      <AlignSettingsBlock config={config} settingsEnabled={settingsEnabled} />
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
          { id: 'align', label: t('bed.level_tab'), icon: <Icon n="target" /> },
          { id: 'plates', label: t('bed.build_plates'), icon: <Icon n="layers" /> },
          { id: 'nozzle', label: t('nozzle.tab'), icon: <Icon n="temp" /> },
        ]}
        value={bed}
        onChange={id => openPage('bed', id as BedTab)}
      />
      {bed === 'align' ? (
        <Levelling />
      ) : bed === 'plates' ? (
        <Plates />
      ) : bed === 'nozzle' ? (
        <NozzleExpansion />
      ) : (
        // Mesh tab; the load cell (the sensor that measures the mesh) sits at the bottom of its card. Old #bed/cell links land here.
        <MeshCard />
      )}
    </Page>
  )
}
