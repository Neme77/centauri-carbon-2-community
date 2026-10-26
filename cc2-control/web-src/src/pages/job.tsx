import { Maximize2, Pause, Play, Square } from 'lucide-preact'
import { useEffect, useState } from 'preact/hooks'
import { ObjectBedMap } from '@/components/object-bed-map'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { CameraCard, Progress } from '@/components/shared'
import { control } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { stateText } from '@/lib/machine'
import { duration } from '@/lib/format'
import { usePoll } from '@/lib/poll'
import { printer, refreshPrinter, view } from '@/lib/state'
import { objects, refreshObjects } from '@/lib/objects'

const I = { size: 16, strokeWidth: 1 }

export const JobControls = ({ v }: { v: ReturnType<typeof view> }) => (
  <div class="mt-4 grid grid-cols-3 gap-2.5">
    <Button class="min-w-0 px-2" disabled={!v.printing} onClick={() => control('print:pause')}>
      <Pause {...I} />
      {t('job.pause')}
    </Button>
    <Button class="min-w-0 px-2" disabled={!v.paused} onClick={() => control('print:resume')}>
      <Play {...I} />
      {t('job.resume')}
    </Button>
    <Button
      class="min-w-0 px-2"
      disabled={!(v.printing || v.paused)}
      onClick={() => control('print:cancel', t('job.cancel_the_active_print'))}
    >
      <Square {...I} />
      {t('common.cancel')}
    </Button>
  </div>
)

export const Job = () => {
  const d = printer.use().data
  const o = objects.use()
  const v = view(d)
  const [mapOpen, setMapOpen] = useState(false)
  usePoll(refreshPrinter, 1500)
  usePoll(refreshObjects, 2000)

  const live = v.active && o.has && o.list.length > 0
  useEffect(() => setMapOpen(false), [v.rawFilename, live])
  const endLabel = o.error
    ? t('job.objects_unavailable')
    : v.active && o.has
      ? tpl('job.n_objects_detected', { n: o.list.length })
      : t('job.no_objects_detected')
  const exclude = async () => {
    if (!v.active || !o.selected || !o.list.includes(o.selected) || o.excluded.includes(o.selected)) return
    const name = o.selected
    const label = o.entries.find(e => e.id === name)?.label || name
    if (await control(`object:exclude:${name}`, tpl('job.exclude_name_from_this_print_this', { name: label }))) {
      objects.set({ selected: '' })
      setTimeout(refreshObjects, 800)
    }
  }
  const stats: [any, string][] = [
    [v.elapsedText, t('common.elapsed')],
    [v.remainingText, tpl('common.remaining_ends_time', { time: v.finishText })],
    [v.active ? v.layer || '—' : '—', t('common.current_layer')],
    [v.total, t('common.total_layers')],
    [v.active ? duration(v.projected) : '—', t('job.est_total_print_time')],
    [live && o.current ? o.entries.find(e => e.id === o.current)?.label || o.current : '—', t('job.current_object')],
  ]
  return (
    <Page title="common.job" sub="job.live_progress_camera_and_objects">
      <div class="grid gap-3.5">
        <div class="cc2-job-primary grid gap-3.5 cc2-lg:grid-cols-2">
          <CameraCard tall />
          <Card class="flex flex-col">
            <CardHead icon="file" title="common.current_job" end={<span class="text-cyan">{stateText(v)}</span>} />
            <div
              title={v.rawFilename || undefined}
              class="cc2-job-filename mb-3 mt-1 text-2xl font-semibold leading-snug [overflow-wrap:anywhere]"
            >
              {v.rawFilename || t('common.no_active_file')}
            </div>
            {!v.active && (
              <a href="#files" class="mb-3 -mt-1 w-fit text-[13px] text-cyan underline underline-offset-2">
                {t('common.choose_a_file_to_print')}
              </a>
            )}
            <Progress pct={v.progress} />
            <div class="cc2-job-stats mt-5 grid grid-cols-2 gap-x-2.5 gap-y-4 cc2-sm:grid-cols-3">
              {stats.map(([val, label], i) => (
                <div key={i} class="min-w-0">
                  <strong class="block text-[15px] [overflow-wrap:anywhere]">{val}</strong>
                  <small class="text-xs text-muted">{label}</small>
                </div>
              ))}
            </div>
            <div class="mt-auto">
              <JobControls v={v} />
            </div>
          </Card>
        </div>
        <Card class="min-w-0">
          <CardHead
            icon="cube"
            title="job.object_exclusion"
            end={
              <span class="flex flex-wrap items-center justify-end gap-2">
                <span>{endLabel}</span>
                <Button disabled={!live} onClick={() => setMapOpen(true)}>
                  <Maximize2 {...I} /> {t('job.bed_map_expand')}
                </Button>
              </span>
            }
          />
          {!mapOpen && <ObjectBedMap live={live} onExclude={() => void exclude()} />}
        </Card>
      </div>
      {mapOpen && live && (
        <ObjectBedMap live={live} onClose={() => setMapOpen(false)} onExclude={() => void exclude()} />
      )}
    </Page>
  )
}
