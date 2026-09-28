import { useEffect, useMemo, useRef, useState } from 'preact/hooks'
import { ArrowBigDown, ArrowBigRight, ArrowBigUp, ArrowLeftRight, Sigma } from 'lucide-preact'
import { cn } from '@/lib/utils'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Dot, Tag } from '@/components/ui/badge'
import { Select } from '@/components/ui/field'
import { Icon } from '@/components/icons'
import { Notice } from '@/components/shared'
import { control, errText, notify, request } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { signed } from '@/lib/format'
import { matrixFromUds, meshRoot, type Pt } from '@/lib/mesh'
import { defaultCam, drawMesh, meshStats, type Cam } from '@/lib/meshdraw'
import { microns, SCREW_KEYS, screwPlan, screwValues } from '@/lib/screws'
import { nav, openPage, screwText } from '@/lib/state'
import { sendConsole } from '@/pages/console'

const I = { size: 16, strokeWidth: 1 }
const PROFILES: [string, string][] = [['default', 'Side A · default'], ['default1', 'Side B · default1'], ['ADAPTIVE', 'Adaptive mesh · ADAPTIVE']]
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
  const st = meshStats(points), root = meshRoot(data)

  const redraw = () => { if (canvas.current && view !== 'values') drawMesh(canvas.current, points, view, cam.current) }
  useEffect(redraw, [points, view, scale])
  useEffect(() => { addEventListener('resize', redraw); return () => removeEventListener('resize', redraw) }, [points, view])

  const load = async () => {
    if (busy.current) return
    busy.current = true
    try {
      const d = await request('/api/mesh')
      if (!meshRoot(d)) throw Error(t('The firmware did not expose bed mesh data'))
      setData(d)
    } catch (e) { notify(tpl('Mesh unavailable: {error}', { error: errText(e) })) } finally { busy.current = false }
  }
  useEffect(() => { load(); const id = setInterval(load, 5000); return () => clearInterval(id) }, [])

  const names = PROFILES.filter(([n]) => root?.profiles?.[n])
  const drag = useRef<{ id: number; x: number; y: number } | null>(null)
  useEffect(() => {
    const el = canvas.current
    if (!el) return
    const wheel = (e: WheelEvent) => { e.preventDefault(); cam.current.zoom = Math.max(0.6, Math.min(1.5, cam.current.zoom * (e.deltaY > 0 ? 0.95 : 1.05))); redraw() }
    el.addEventListener('wheel', wheel, { passive: false })
    return () => el.removeEventListener('wheel', wheel)
  }, [points, view])

  const Stat = ({ dot, label, val }: { dot: any; label: string; val: string }) => (
    <div class="rounded-lg border border-edge bg-field/60 p-2.5"><label class="flex items-center gap-2 text-xs">{dot}{t(label)}</label>
      <strong class="mt-2 block whitespace-nowrap text-2xl font-semibold">{val} <small class="text-xs">mm</small></strong>
      <p class="mt-1 text-[10px] text-muted">{profile === 'active' ? t('Current printer mesh') : `${t('Saved profile')} · ${profile}`}</p></div>
  )
  return (
    <Card>
      <div class="mb-3 flex flex-wrap items-center gap-3">
        <Icon n="cube" class="text-cyan" />
        <div class="mr-auto"><h2 class="text-xl font-semibold">{t('Bed Mesh 3D')}</h2><p class="text-xs text-muted">{points.length ? tpl('{n} live probe points', { n: points.length }) : t('Waiting for live mesh data')}</p></div>
        <label class="flex items-center gap-2 text-xs">{t('Mesh profile')}
          <Select class="w-auto" value={profile} onChange={e => setProfile(e.currentTarget.value)}>
            <option value="active">{t('Active mesh')}{root?.profile_name ? ` · ${root.profile_name}` : ''}</option>{names.map(([n, l]) => <option key={n} value={n}>{l}</option>)}
          </Select></label>
        <div class="flex">{(['3d', '2d', 'values'] as View[]).map((v, i) => <Button key={v} variant={view === v ? 'primary' : 'default'} class={cn('text-xs', i === 0 ? 'rounded-r-none' : i === 2 ? 'rounded-l-none' : 'rounded-none')} onClick={() => setView(v)}>{t(v === '3d' ? 'View 3D' : v === '2d' ? 'View 2D' : 'Values')}</Button>)}</div>
        <label class="flex items-center gap-2 border-l border-edge pl-3 text-xs">{t('Z Scale')}<input class="w-18" type="range" min=".3" max="2" step=".1" value={scale} onInput={e => { cam.current.scale = +e.currentTarget.value; setScale(cam.current.scale) }} /><span>{scale.toFixed(1)}×</span></label>
      </div>
      <div class="overflow-hidden rounded-lg bg-[#031521]">
        {view === 'values' ? (
          <div class="h-[430px] overflow-auto p-3 font-mono text-[11px] text-[#edf6fc]">
            {points.length < 4 ? <p class="text-muted">{t('Waiting for live mesh data.')}</p> : (
              <table class="w-full border-collapse"><caption class="mb-2 text-left">{t('Live Z heights in mm · Y descending')}</caption>
                <thead><tr><th class="p-1 text-left font-normal">Y / X</th>{Array.from({ length: 11 }, (_, i) => <th key={i} class="p-1 text-left font-normal">{i * 25}</th>)}</tr></thead>
                <tbody>{Array.from({ length: 11 }, (_, r) => 10 - r).map(y => <tr key={y}><th class="p-1 text-left">{y * 25}</th>{Array.from({ length: 11 }, (_, x) => <td key={x} class="p-1">{points[y * 11 + x]?.z.toFixed(3)}</td>)}</tr>)}</tbody></table>)}
          </div>
        ) : (
          <canvas ref={canvas} aria-label="Interactive live bed mesh" class="block h-[430px] w-full cursor-grab touch-none active:cursor-grabbing"
            onPointerDown={e => { drag.current = { id: e.pointerId, x: e.clientX, y: e.clientY }; e.currentTarget.setPointerCapture(e.pointerId) }}
            onPointerMove={e => { const d = drag.current; if (!d || d.id !== e.pointerId) return; cam.current.yaw += (e.clientX - d.x) * 0.005; cam.current.pitch = Math.max(0.25, Math.min(1.2, cam.current.pitch + (e.clientY - d.y) * 0.004)); drag.current = { id: e.pointerId, x: e.clientX, y: e.clientY }; redraw() }}
            onPointerUp={() => { drag.current = null }} onPointerCancel={() => { drag.current = null }} />
        )}
        <div class="p-2 text-center text-[11px] text-muted">{t('Drag to rotate · scroll to zoom ·')} <button type="button" class="text-[#72d7eb]" onClick={() => { cam.current = defaultCam(); setScale(1); redraw() }}>{t('Reset view')}</button></div>
      </div>
      <div class="mt-3 grid grid-cols-2 gap-2.5 md:grid-cols-4">
        <Stat dot={<Dot c="blue" />} label="Minimum" val={st ? signed(st.min) : '—'} /><Stat dot={<Dot c="amber" />} label="Maximum" val={st ? signed(st.max) : '—'} />
        <Stat dot={<ArrowLeftRight {...I} class="text-cyan" />} label="Range" val={st ? st.range.toFixed(3) : '—'} /><Stat dot={<Sigma {...I} class="text-cyan" />} label="Average" val={st ? signed(st.mean) : '—'} />
      </div>
      <MeshActions reload={load} note={points.length ? (profile === 'active' ? `${t('Active mesh loaded')}${root?.profile_name ? ' · ' + root.profile_name : ''}.` : `${t('Saved mesh loaded')} · ${profile}.`) : t('Waiting for the printer mesh.')} />
    </Card>
  )
}

const MeshActions = ({ reload, note }: { reload: () => void; note: string }) => (
  <>
    <div class="mt-3 grid gap-3 sm:grid-cols-[1fr_1.15fr]">
      {[['folder', 'Load Current Mesh', 'Read the saved mesh from printer memory.', reload, false], ['bolt', 'Run Bed Mesh Calibration', 'Start a protected calibration when idle.', () => confirm(t('Start a new bed mesh calibration at 60 °C?')) && sendConsole('BED_MESH_CALIBRATE PROFILE=default BED_TEMP=60'), true]].map(([icon, title, sub, fn, primary]: any) => (
        <Button key={title} variant={primary ? 'primary' : 'default'} class="h-auto items-start justify-start gap-3.5 p-3 text-left" onClick={fn}><Icon n={icon} class="size-7" /><span><strong class="block text-sm">{t(title)}</strong><small class="mt-1 block whitespace-normal text-xs font-normal">{t(sub)}</small></span></Button>
      ))}
    </div>
    <Notice>{note}</Notice>
  </>
)

const Screws = () => {
  const values = screwValues(screwText.use().text)
  const plan = values ? screwPlan(values) : null
  const rows: [string, string, string][] = [['FL', '35,30', '1'], ['FR', '225,30', '2'], ['RR', '225,225', '3'], ['RL', '35,225', '4']]
  const plate: [number, string, string, string][] = [[3, '4', 'RL', 'X35 Y225'], [2, '3', 'RR', 'X225 Y225'], [0, '1', 'FL', 'X35 Y30'], [1, '2', 'FR', 'X225 Y30']]
  return (
    <Card id="screwFocus" class="flex-1">
      <CardHead icon="target" title="Four-Screw Leveling" end={<Tag tone="warning">{t(plan ? 'Measured results' : 'No measurement')}</Tag>} />
      <p class="mb-3 text-xs text-muted">{t('Nozzle load-cell measurement at four positions. Not a replacement for bed mesh.')}</p>
      <div class="grid gap-3 sm:grid-cols-[.9fr_1.1fr]">
        <div class="rounded-lg border border-edge p-3">
          <div class="relative grid h-48 grid-cols-2 grid-rows-2 rounded-xl border-2 border-muted">
            <i class="absolute inset-y-2 left-1/2 border-l border-dashed border-edge" /><i class="absolute inset-x-2 top-1/2 border-t border-dashed border-edge" />
            {plate.map(([i, n, name, xy]) => {
              const v = plan ? plan.shown[i] : 0, tint = plan ? `rgba(${v > 0 ? '20,207,233' : '255,123,134'},${(0.06 + Math.min(1, Math.abs(v) / 0.2) * 0.2).toFixed(2)})` : undefined
              return <div key={n} class="relative flex flex-col items-center justify-center rounded-lg text-xs" style={{ background: tint }}><b class="mb-0.5 grid size-6.5 place-items-center rounded-full border border-muted font-medium">{n}</b>{name}<small class="text-[10px] text-muted">{xy}</small>{plan && <span class="mt-0.5 text-[13px] font-bold">{microns(v)}</span>}</div>
            })}
          </div>
          <div class="mt-1.5 text-center text-[10px] text-muted">{t('Top view · front edge at bottom')}</div>
          <div class="mt-2 flex justify-between text-[10px] text-muted"><span class="flex items-center gap-1"><ArrowBigUp size={12} strokeWidth={1} /> Y (back)</span><span class="flex items-center gap-1"><ArrowBigRight size={12} strokeWidth={1} /> X (right)</span></div>
        </div>
        <div>
          <div class="overflow-hidden rounded-md border border-edge"><table class="w-full border-collapse text-xs">
            <thead><tr class="text-left text-[11px] text-muted"><th class="p-2 font-normal">{t('Position')}</th><th class="p-2 font-normal">{t('Offset')}</th><th class="p-2 font-normal">{t('Adjustment')}</th></tr></thead>
            <tbody>{rows.map(([name, key], i) => {
              const d = plan?.shown[i] ?? 0, within = Math.abs(d) <= 0.02, ref = plan && !plan.useOptimized && i === 0
              return <tr key={name} class={cn('border-t border-edge', plan?.useOptimized && i === 0 && 'bg-amber/10')}><td class="p-2 font-semibold">{name}</td>
                <td class="p-2">{plan ? (ref ? t('Reference') : microns(d)) : '—'}</td>
                <td class={cn('p-2', !within && plan && !ref && (d > 0 ? 'text-cyan' : 'text-red'))}>{plan ? (ref ? '—' : within ? t('Within tolerance') : d > 0 ? <span class="flex items-center gap-1"><ArrowBigDown {...I} />{t('Lower')}</span> : <span class="flex items-center gap-1"><ArrowBigUp {...I} />{t('Raise')}</span>) : '—'}</td></tr>
            })}</tbody></table></div>
          <Notice><span>{t('3 samples per point')}<br />{t('Front-left reference')}<br />{t('Mesh capture stays separate')}</span></Notice>
        </div>
      </div>
      {plan?.useOptimized && <Notice icon="info">{tpl('Optimized reference adjustment: {word} FL by {amount} µm. Maximum suggested movement falls from {before} µm to {best} µm.', { word: t(plan.common > 0 ? 'lower' : 'raise'), amount: Math.abs(Math.round(plan.common * 1000)), before: plan.before, best: plan.best })}</Notice>}
      <div class="mt-3"><Button onClick={async () => { if (await control('screws:measure', t('Start the four-screw load-cell measurement?'))) openPage('bed', true) }}><Icon n="target" class="size-5" />{t('Measure Screws')}</Button></div>
    </Card>
  )
}

export const Bed = () => {
  const { screws } = nav.use()
  useEffect(() => { if (screws) document.getElementById('screwFocus')?.scrollIntoView({ block: 'center', behavior: 'smooth' }) }, [screws])
  const tab = (on: boolean) => cn('flex items-center gap-3 rounded-t-lg border border-b-[3px] border-edge px-6 py-3 text-base', on ? 'border-b-cyan bg-field text-cyan' : 'border-b-transparent text-muted')
  return (
    <Page title="Bed Levelling" sub="Mesh, saved profiles and four-screw adjustment in one workflow">
      <div class="mb-3.5 flex border-b border-edge">
        <button type="button" class={tab(!screws)} onClick={() => openPage('bed')}><Icon n="grid" />{t('Mesh & Saved Profiles')}</button>
        <button type="button" class={tab(screws)} onClick={() => openPage('bed', true)}><Icon n="target" />{t('Screw Levelling')}</button>
      </div>
      <div class="grid items-start gap-3.5 xl:grid-cols-[1.32fr_1fr]"><MeshCard /><Screws /></div>
    </Page>
  )
}
