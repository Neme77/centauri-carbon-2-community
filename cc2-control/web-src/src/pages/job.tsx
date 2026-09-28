import { useEffect } from 'preact/hooks'
import { ArrowUpRight, Diamond, Pause, Play, Square, TriangleAlert, X } from 'lucide-preact'
import { cn } from '@/lib/utils'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Dot, Tag } from '@/components/ui/badge'
import { CameraCard, FanBar, Progress, Reading, Row } from '@/components/shared'
import { control } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { num, duration } from '@/lib/format'
import { openPage, printer, view, zoffset } from '@/lib/state'
import { objects, refreshObjects } from '@/lib/objects'

const I = { size: 16, strokeWidth: 1 }

export const JobControls = ({ v }: { v: ReturnType<typeof view> }) => (
  <div class="mt-4 flex gap-2.5">
    <Button class="flex-1" disabled={!v.printing} onClick={() => control('print:pause')}><Pause {...I} />{t('Pause')}</Button>
    <Button class="flex-1" disabled={!v.paused} onClick={() => control('print:resume')}><Play {...I} />{t('Resume')}</Button>
    <Button class="flex-1" disabled={!(v.printing || v.paused)} onClick={() => control('print:cancel', t('Cancel the active print?'))}><Square {...I} />{t('Cancel')}</Button>
  </div>
)

export const Job = () => {
  const d = printer.use().data
  const o = objects.use()
  const off = zoffset.use().v
  const v = view(d)
  useEffect(() => { refreshObjects(); const id = setInterval(refreshObjects, 1000); return () => clearInterval(id) }, [])

  const live = v.active && o.has && o.list.length > 0
  const summary = o.error ? t('Object data unavailable') : v.active && o.list.length ? `${o.list.length} ${t(o.list.length === 1 ? 'object' : 'objects')} · ${o.excluded.length} ${t('excluded')}` : t('No labelled objects reported')
  const endLabel = o.error ? t('Objects unavailable') : v.active && o.has ? tpl('{n} objects detected', { n: o.list.length }) : t('No objects detected')
  const exclude = async () => {
    if (!o.selected) return
    const name = o.selected
    if (await control(`object:exclude:${name}`, tpl('Exclude “{name}” from this print? This cannot be undone.', { name }))) { objects.set({ selected: '' }); setTimeout(refreshObjects, 800) }
  }
  return (
    <div class="grid gap-3.5">
      <div class="grid gap-3.5 lg:grid-cols-2 xl:grid-cols-[1.25fr_.72fr_1fr]">
        <Card class="flex flex-col"><CardHead icon="camera" title="Live Camera" end={<><Dot /> {t('Live')}</>} /><CameraCard tall /></Card>
        <Card>
          <CardHead icon="file" title="Current Job" end={<span class="text-cyan">{t(v.state)}</span>} />
          <div class="text-lg font-semibold leading-snug [overflow-wrap:anywhere]">{v.rawFilename === 'No active file' ? t(v.rawFilename) : v.rawFilename}</div>
          <p>{`${t('Layer')} ${v.layer || '—'} / ${v.total}`}</p><p class="text-muted">{summary}</p>
          <div class="mt-4"><Progress pct={v.progress} /></div>
          <div class="mt-5 grid grid-cols-2 gap-2.5">
            {[[v.elapsedText, t('Elapsed')], [v.remainingText, tpl('Remaining · ends {time}', { time: v.finishText })], [v.active ? v.layer || '—' : '—', t('Current layer')], [v.total, t('Total layers')]].map(([val, label], i) => (
              <div key={i}><strong class="block text-[15px]">{val}</strong><small class="text-[11px] text-muted">{label}</small></div>
            ))}
          </div>
          <JobControls v={v} />
        </Card>
        <Card class="min-w-0 lg:col-span-2 xl:col-span-1">
          <CardHead icon="cube" title="Object Exclusion" end={endLabel} />
          <div class="grid min-h-40 grid-cols-2 gap-3 rounded-md border border-edge p-4">
            {o.list.length === 0 ? <span class="col-span-2 text-muted">{t('No live object data')}</span> : o.list.map(n => {
              const x = o.excluded.includes(n)
              return <div key={n} class={cn('flex min-w-0 flex-col items-center text-center', x && 'text-red')}><i class={cn('grid h-12 w-16 place-items-center rounded-lg border-4 text-xl not-italic', x ? 'border-red' : n === o.selected ? 'border-cyan' : 'border-muted')}>{x ? <X size={24} strokeWidth={1} /> : <Diamond size={24} strokeWidth={1} />}</i><span class="mt-1 text-[10px] [overflow-wrap:anywhere]">{n}</span></div>
            })}
          </div>
          <div class="mt-3 grid max-h-52 gap-2 overflow-y-auto">
            {o.list.length === 0 ? <span class="text-muted">{t(o.error ? 'Object status unavailable' : v.active ? 'No labelled objects reported by the active print.' : 'Objects will appear during a supported print.')}</span> : o.list.map(n => {
              const x = o.excluded.includes(n)
              return <Button key={n} variant={n === o.selected ? 'active' : 'default'} class={cn('justify-start text-left text-xs [overflow-wrap:anywhere] h-auto whitespace-normal', x && 'text-red')} disabled={x} onClick={() => objects.set({ selected: n })}>{x ? <Diamond {...I} /> : <Diamond {...I} fill="currentColor" />} {n}{x ? ` · ${t('Excluded')}` : ''}</Button>
            })}
          </div>
          <Button wide variant="primary" class="mt-3 border-amber bg-amber" disabled={!o.selected} onClick={exclude}><TriangleAlert {...I} />{t('Exclude Selected Object')}</Button>
          <p class="mt-2 text-center text-muted">{t('This protected action cannot be undone.')}</p>
        </Card>
      </div>
      <div class="grid grid-cols-2 gap-3 lg:grid-cols-3 xl:grid-cols-4">
        <Card><CardHead icon="temp" title="Temperatures" /><div class="grid gap-3"><Reading dot="red" label="Nozzle" value={`${num(d?.extruder?.temperature)} °C`} /><Reading dot="amber" label="Bed" value={`${num(d?.heater_bed?.temperature)} °C`} /><Reading dot="blue" label="Chamber" value={`${num(d?.chamber?.temperature)} °C`} /></div></Card>
        <Card><CardHead icon="fan" title="Fans" /><FanBar label="Part" pct={v.fan('part')} /><FanBar label="Aux" pct={v.fan('aux')} /><FanBar label="Chamber" pct={v.fan('box')} /></Card>
        <Card class="flex flex-col"><CardHead icon="z" title="Live Z Offset" /><div class="my-2 text-3xl">{(off > 0 ? '+' : '') + off.toFixed(2)} <small class="text-sm text-muted">mm</small></div><Button wide class="mt-auto" onClick={() => openPage('control')}>{t('Adjust in Control')}<ArrowUpRight {...I} /></Button></Card>
        <Card><CardHead title="Object Statistics" />
          <Row label="Objects" value={live ? o.list.length : '—'} /><Row label="Printing" value={live ? Math.max(0, o.list.length - o.excluded.length) : '—'} /><Row label="Excluded" value={live ? o.excluded.length : '—'} />
          <Row label="Current Object" value={live && o.current ? o.current : '—'} /><Row label="Est. Total Print Time" value={v.active ? duration(v.projected) : '—'} /></Card>
      </div>
    </div>
  )
}
