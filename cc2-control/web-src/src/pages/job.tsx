import { useEffect } from 'preact/hooks'
import { Diamond, Pause, Play, Square, TriangleAlert, X } from 'lucide-preact'
import { cn } from '@/lib/utils'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Dot } from '@/components/ui/badge'
import { CameraCard, Progress } from '@/components/shared'
import { control } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { duration } from '@/lib/format'
import { printer, view } from '@/lib/state'
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
  const v = view(d)
  useEffect(() => { refreshObjects(); const id = setInterval(refreshObjects, 1000); return () => clearInterval(id) }, [])

  const live = v.active && o.has && o.list.length > 0
  const endLabel = o.error ? t('Objects unavailable') : v.active && o.has ? tpl('{n} objects detected', { n: o.list.length }) : t('No objects detected')
  const exclude = async () => {
    if (!o.selected) return
    const name = o.selected
    if (await control(`object:exclude:${name}`, tpl('Exclude “{name}” from this print? This cannot be undone.', { name }))) { objects.set({ selected: '' }); setTimeout(refreshObjects, 800) }
  }
  const stats: [any, string][] = [
    [v.elapsedText, t('Elapsed')], [v.remainingText, tpl('Remaining · ends {time}', { time: v.finishText })],
    [v.active ? v.layer || '—' : '—', t('Current layer')], [v.total, t('Total layers')],
    [v.active ? duration(v.projected) : '—', t('Est. Total Print Time')], [live && o.current ? o.current : '—', t('Current Object')],
  ]
  return (
    <div class="grid gap-3.5">
      <div class="grid gap-3.5 lg:grid-cols-2">
        <Card class="flex flex-col"><CardHead icon="camera" title="Live Camera" end={<><Dot /> {t('Live')}</>} /><CameraCard tall /></Card>
        <Card class="flex flex-col">
          <CardHead icon="file" title="Current Job" end={<span class="text-cyan">{t(v.state)}</span>} />
          <div class="mb-3 mt-1 text-2xl font-semibold leading-snug [overflow-wrap:anywhere]">{v.rawFilename === 'No active file' ? t(v.rawFilename) : v.rawFilename}</div>
          <Progress pct={v.progress} />
          <div class="mt-5 grid grid-cols-2 gap-x-2.5 gap-y-4 sm:grid-cols-3">
            {stats.map(([val, label], i) => <div key={i} class="min-w-0"><strong class="block text-[15px] [overflow-wrap:anywhere]">{val}</strong><small class="text-[11px] text-muted">{label}</small></div>)}
          </div>
          <div class="mt-auto"><JobControls v={v} /></div>
        </Card>
      </div>
      <Card class="min-w-0">
        <CardHead icon="cube" title="Object Exclusion" end={endLabel} />
        <div class="grid gap-3.5 lg:grid-cols-2">
          <div class="grid min-h-40 grid-cols-2 content-start gap-3 rounded-md border border-edge p-4 sm:grid-cols-3 lg:grid-cols-2 xl:grid-cols-4">
            {o.list.length === 0 ? <span class="col-span-full text-muted">{t('No live object data')}</span> : o.list.map(n => {
              const x = o.excluded.includes(n)
              return <div key={n} class={cn('flex min-w-0 flex-col items-center text-center', x && 'text-red')}><i class={cn('grid h-12 w-16 place-items-center rounded-lg border-4 not-italic', x ? 'border-red' : n === o.selected ? 'border-cyan' : 'border-muted')}>{x ? <X size={24} strokeWidth={1} /> : <Diamond size={24} strokeWidth={1} />}</i><span class="mt-1 text-[10px] [overflow-wrap:anywhere]">{n}</span></div>
            })}
          </div>
          <div class="flex flex-col">
            <div class="grid max-h-52 gap-2 overflow-y-auto">
              {o.list.length === 0 ? <span class="text-muted">{t(o.error ? 'Object status unavailable' : v.active ? 'No labelled objects reported by the active print.' : 'Objects will appear during a supported print.')}</span> : o.list.map(n => {
                const x = o.excluded.includes(n)
                return <Button key={n} variant={n === o.selected ? 'active' : 'default'} class={cn('h-auto justify-start whitespace-normal text-left text-xs [overflow-wrap:anywhere]', x && 'text-red')} disabled={x} onClick={() => objects.set({ selected: n })}>{x ? <Diamond {...I} /> : <Diamond {...I} fill="currentColor" />} {n}{x ? ` · ${t('Excluded')}` : ''}</Button>
              })}
            </div>
            <Button wide variant="primary" class="mt-3 border-amber bg-amber" disabled={!o.selected} onClick={exclude}><TriangleAlert {...I} />{t('Exclude Selected Object')}</Button>
            <p class="mt-2 text-center text-muted">{t('This protected action cannot be undone.')}</p>
          </div>
        </div>
      </Card>
    </div>
  )
}
