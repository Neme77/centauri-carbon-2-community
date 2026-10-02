import { Diamond, Pause, Play, Square, TriangleAlert, X } from 'lucide-preact'
import { cn } from '@/lib/utils'
import { PrintTuning } from '@/components/print-tuning'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { CameraCard, Progress } from '@/components/shared'
import { control } from '@/lib/api'
import { t, tState, tpl } from '@/lib/i18n'
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
  usePoll(refreshPrinter, 1500)
  usePoll(refreshObjects, 2000)

  const live = v.active && o.has && o.list.length > 0
  const endLabel = o.error
    ? t('job.objects_unavailable')
    : v.active && o.has
      ? tpl('job.n_objects_detected', { n: o.list.length })
      : t('job.no_objects_detected')
  const exclude = async () => {
    if (!o.selected) return
    const name = o.selected
    if (await control(`object:exclude:${name}`, tpl('job.exclude_name_from_this_print_this', { name }))) {
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
    [live && o.current ? o.current : '—', t('job.current_object')],
  ]
  return (
    <Page title="common.job" sub="job.live_progress_camera_and_objects">
      <div class="grid gap-3.5">
        <div class="cc2-job-primary grid gap-3.5 cc2-lg:grid-cols-2">
          <CameraCard tall />
          <Card class="flex flex-col">
            <CardHead icon="file" title="common.current_job" end={<span class="text-cyan">{tState(v.state)}</span>} />
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
        <PrintTuning />
        <Card class="min-w-0">
          <CardHead icon="cube" title="job.object_exclusion" end={endLabel} />
          <div class="grid gap-3.5 cc2-lg:grid-cols-2">
            <div class="grid min-h-40 grid-cols-2 content-start gap-3 rounded-md border border-edge p-4 cc2-sm:grid-cols-3 cc2-lg:grid-cols-2 cc2-xl:grid-cols-4">
              {o.list.length === 0 ? (
                <span class="col-span-full text-muted">{t('job.no_live_object_data')}</span>
              ) : (
                o.list.map(n => {
                  const x = o.excluded.includes(n)
                  return (
                    <div key={n} class={cn('flex min-w-0 flex-col items-center text-center', x && 'text-red')}>
                      <i
                        class={cn(
                          'grid h-12 w-16 place-items-center rounded-lg border-4 not-italic',
                          x ? 'border-red' : n === o.selected ? 'border-cyan' : 'border-muted'
                        )}
                      >
                        {x ? <X size={24} strokeWidth={1} /> : <Diamond size={24} strokeWidth={1} />}
                      </i>
                      <span class="mt-1 text-xs [overflow-wrap:anywhere]">{n}</span>
                    </div>
                  )
                })
              )}
            </div>
            <div class="flex flex-col">
              <div class="grid max-h-52 gap-2 overflow-y-auto">
                {o.list.length === 0 ? (
                  <span class="text-muted">
                    {t(
                      o.error
                        ? 'job.object_status_unavailable'
                        : v.active
                          ? 'job.no_labelled_objects_reported_by'
                          : 'job.objects_will_appear_during_a'
                    )}
                  </span>
                ) : (
                  o.list.map(n => {
                    const x = o.excluded.includes(n)
                    return (
                      <Button
                        key={n}
                        variant={n === o.selected ? 'active' : 'default'}
                        class={cn(
                          'h-auto justify-start whitespace-normal text-left text-xs [overflow-wrap:anywhere]',
                          x && 'text-red'
                        )}
                        disabled={x}
                        onClick={() => objects.set({ selected: n })}
                      >
                        {x ? <Diamond {...I} /> : <Diamond {...I} fill="currentColor" />} {n}
                        {x ? ` · ${t('job.excluded')}` : ''}
                      </Button>
                    )
                  })
                )}
              </div>
              <Button
                wide
                variant="primary"
                class="mt-3 border-amber bg-amber"
                disabled={!o.selected}
                onClick={exclude}
              >
                <TriangleAlert {...I} />
                {t('job.exclude_selected_object')}
              </Button>
              <p class="mt-2 text-center text-muted">{t('job.this_protected_action_cannot_be')}</p>
            </div>
          </div>
        </Card>
      </div>
    </Page>
  )
}
