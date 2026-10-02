import { useEffect, useMemo, useRef, useState } from 'preact/hooks'
import { ask } from '@/lib/confirm'
import { ArrowBigDown, ArrowBigRight, ArrowBigUp, ArrowLeftRight, Sigma } from 'lucide-preact'
import { cn } from '@/lib/utils'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Dot, Tag } from '@/components/ui/badge'
import { Select } from '@/components/ui/field'
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
import { nav, openPage, refreshConsole, screwText } from '@/lib/state'
import { sendConsole } from '@/pages/console'

const I = { size: 16, strokeWidth: 1 }
const PROFILES: [string, Key][] = [
  ['default', 'bed.side_a_default'],
  ['default1', 'bed.side_b_default1'],
  ['ADAPTIVE', 'bed.adaptive_mesh_adaptive'],
]
type View = '3d' | '2d' | 'values'

const MeshCard = () => {
  const [data, setData] = useState<any>(null)
  const [profile, setProfile] = useState('active')
  const [view, setView] = useState<View>('3d')
  const [scale, setScale] = useState(1)
  const cam = useRef<Cam>(defaultCam())
  const canvas = useRef<HTMLCanvasElement>(null)
  const busy = useRef(false)
  const points: Pt[] = useMemo(() => (data && matrixFromUds(data, profile)) || [], [data, profile])
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
    addEventListener('resize', redraw)
    return () => removeEventListener('resize', redraw)
  }, [points, view])

  const warned = useRef(false) // one error toast per outage, not one every 5 s
  const load = async (manual = false) => {
    if (busy.current) return
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
    }
  }
  useEffect(() => {
    void load()
  }, [])

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
    <div class="rounded-lg border border-edge bg-field/60 p-2.5">
      <span class="flex items-center gap-2 text-xs">
        {dot}
        {t(label)}
      </span>
      <strong class="mt-2 block whitespace-nowrap text-2xl font-semibold">
        {val} <small class="text-xs">mm</small>
      </strong>
      <p class="mt-1 text-xs text-muted">
        {profile === 'active' ? t('bed.current_printer_mesh') : `${t('bed.saved_profile')} · ${profile}`}
      </p>
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
                    {Array.from({ length: 11 }, (_, i) => (
                      <th key={i} class="p-1 text-left font-normal">
                        {i * 25}
                      </th>
                    ))}
                  </tr>
                </thead>
                <tbody>
                  {Array.from({ length: 11 }, (_, r) => 10 - r).map(y => (
                    <tr key={y}>
                      <th class="p-1 text-left">{y * 25}</th>
                      {Array.from({ length: 11 }, (_, x) => (
                        <td key={x} class="p-1">
                          {points[y * 11 + x]?.z.toFixed(3)}
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
              : `${t('bed.saved_mesh_loaded')} · ${profile}.`
            : t('bed.waiting_for_the_printer_mesh')
        }
      />
    </Card>
  )
}

const MeshActions = ({ reload, note }: { reload: () => void; note: string }) => (
  <>
    <div class="mt-3 grid gap-3 cc2-sm:grid-cols-[1fr_1.15fr]">
      {[
        ['folder', 'bed.load_current_mesh', 'bed.read_the_saved_mesh_from_printer', reload, false],
        [
          'bolt',
          'bed.run_bed_mesh_calibration',
          'bed.start_a_protected_calibration_when',
          async () => {
            if (!(await ask(t('bed.start_a_new_bed_mesh_calibration')))) return
            await sendConsole('BED_MESH_CALIBRATE PROFILE=default BED_TEMP=60')
            // The console worker knows exactly when the calibration command has finished.
            // Poll only that local status while calibration is running, then fetch the new mesh once.
            const deadline = Date.now() + 15 * 60_000
            while (Date.now() < deadline) {
              await new Promise(resolve => setTimeout(resolve, 2500))
              try {
                const status = await request('/api/console')
                if (status?.command !== 'BED_MESH_CALIBRATE PROFILE=default BED_TEMP=60') return
                if (status?.completed) {
                  if (status.success) reload()
                  return
                }
              } catch {
                return
              }
            }
          },
          true,
        ],
      ].map(([icon, title, sub, fn, primary]: any) => (
        <Button
          key={title}
          variant={primary ? 'primary' : 'default'}
          class="h-auto items-start justify-start gap-3.5 p-3 text-left"
          onClick={fn}
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

const Screws = () => {
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
            if (await control('screws:measure', t('bed.start_the_four_screw_load_cell'))) openPage('bed', true)
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
  const { screws } = nav.use()
  return (
    <Page title="common.bed_levelling" sub="bed.mesh_saved_profiles_and_four_screw">
      <Tabs
        items={[
          { id: 'mesh', label: t('bed.mesh_saved_profiles'), icon: <Icon n="grid" /> },
          { id: 'screws', label: t('bed.screw_levelling'), icon: <Icon n="target" /> },
        ]}
        value={screws ? 'screws' : 'mesh'}
        onChange={id => openPage('bed', id === 'screws')}
      />
      {screws ? (
        <div class="max-w-4xl">
          <Screws />
        </div>
      ) : (
        <MeshCard />
      )}
    </Page>
  )
}
