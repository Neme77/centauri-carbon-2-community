import { useEffect, useState } from 'preact/hooks'
import { Download, Film, RefreshCw } from 'lucide-preact'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Tag } from '@/components/ui/badge'
import { errText, notify } from '@/lib/api'
import { ask } from '@/lib/confirm'
import { type Key, t, tpl } from '@/lib/i18n'
import { duration, fileSize } from '@/lib/format'
import { history, refreshHistory, renderTimelapse, requestHistory, type Task, timelapseUrl } from '@/lib/history'
import { printerError } from '@/lib/machine'
import { usePoll } from '@/lib/poll'
import { printer, view } from '@/lib/state'

const I = { size: 16, strokeWidth: 1 }
const STEP = 25
const RESULT: Record<number, [Key, 'ok' | 'warning' | 'default']> = {
  1: ['history.completed', 'ok'],
  2: ['history.stopped', 'warning'],
  3: ['history.stopped', 'warning'],
  4: ['history.printing', 'default'],
  5: ['history.paused', 'default'],
}
const when = (s: number) =>
  s > 0 ? new Date(s * 1000).toLocaleString([], { dateStyle: 'short', timeStyle: 'short' }) : '—'
const took = (task: Task) => (task.end > task.begin && task.begin > 0 ? duration(task.end - task.begin) : '—')

// Like G-code downloads: the browser streams the attachment, nothing is buffered in a Blob.
const download = (task: Task) => {
  const link = document.createElement('a')
  link.href = timelapseUrl(task.id)
  link.download = `${task.name.replace(/\.gcode$/i, '') || 'timelapse'}.mp4`
  document.body.append(link)
  link.click()
  link.remove()
}

const Video = ({ task, idle, busy }: { task: Task; idle: boolean; busy: boolean }) => {
  if (task.video === 2)
    return (
      <Button class="h-auto min-w-0 px-2 py-1.5 text-xs" onClick={() => download(task)}>
        <Download {...I} />
        {t('history.download_video')}
        {task.size > 0 && <small class="text-muted">{fileSize(task.size)}</small>}
      </Button>
    )
  if (task.video !== 1 && task.video !== 3) return <small class="text-muted">{t('history.no_time_lapse')}</small>
  // Rendering keeps the printer busy for minutes: Idle only, one video at a time, after a confirmation.
  const render = async () => {
    if (!(await ask(tpl('history.create_the_time_lapse_of_name', { name: task.name })))) return
    try {
      await renderTimelapse(task.id)
      notify(t('history.the_printer_is_creating_the_video'))
      void refreshHistory()
    } catch (e) {
      notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
    }
  }
  return (
    <div class="grid justify-items-end gap-1">
      {task.video === 3 && <small class="text-amber">{t('history.previous_attempt_failed')}</small>}
      <Button
        class="h-auto min-w-0 px-2 py-1.5 text-xs"
        disabled={!idle || busy}
        title={idle ? undefined : t('common.available_when_idle')}
        onClick={render}
      >
        <Film {...I} />
        {t(busy ? 'history.creating_the_video' : 'history.create_video')}
      </Button>
    </div>
  )
}

export const History = () => {
  const h = history.use()
  const v = view(printer.use().data)
  const [shown, setShown] = useState(STEP)
  const [failure, setFailure] = useState('')
  // Reads come from CC2 Control's memory; only Refresh and opening the page ask the printer.
  usePoll(refreshHistory, h.pending || h.generating ? 2000 : 30000)
  const refresh = async () => {
    try {
      setFailure('')
      await requestHistory()
    } catch (e) {
      setFailure(errText(e))
    }
    void refreshHistory()
  }
  useEffect(() => {
    void refresh()
  }, [])
  const status = !h.loaded
    ? t('history.loading')
    : h.pending
      ? t('history.asking_the_printer')
      : failure
        ? tpl('common.rejected_error', { error: failure })
        : h.error === -2
          ? t('history.too_large')
          : h.error > 0
            ? tpl('history.the_printer_did_not_send_its_history', { error: printerError(h.error) })
            : !h.available
              ? t('history.not_loaded')
              : !h.tasks.length
                ? t('history.empty')
                : ''
  const cols =
    'grid grid-cols-[minmax(0,1fr)_auto] items-center gap-x-3 gap-y-1 border-b border-edge px-1 py-2.5 cc2-md:grid-cols-[minmax(0,2.2fr)_1fr_.7fr_.8fr_13rem]'
  return (
    <Page title="common.history" sub="history.jobs_recorded_by_the_printer_and">
      <Card>
        <CardHead
          icon="file"
          title="history.print_jobs"
          end={
            <Button class="min-h-8 px-2 text-xs text-fg" disabled={h.pending} onClick={refresh}>
              <RefreshCw {...I} />
              {t('history.refresh')}
            </Button>
          }
        />
        {status && <p class="py-3 text-muted">{status}</p>}
        {h.tasks.length > 0 && (
          <div class="cc2-history-list">
            <div class={`${cols} hidden text-xs text-muted cc2-md:grid`}>
              <span>{t('common.name')}</span>
              <span>{t('history.started')}</span>
              <span>{t('history.duration')}</span>
              <span>{t('history.result')}</span>
              <span class="justify-self-end">{t('history.time_lapse')}</span>
            </div>
            {h.tasks.slice(0, shown).map(task => {
              const [label, tone] = RESULT[task.status] ?? ['history.unknown', 'default']
              return (
                <div key={task.id} class={cols}>
                  <div class="min-w-0">
                    <div class="font-semibold [overflow-wrap:anywhere]">{task.name || '—'}</div>
                    <small class="text-muted cc2-md:hidden">
                      {when(task.begin)} · {took(task)} · {t(label)}
                    </small>
                  </div>
                  <span class="hidden cc2-md:block">{when(task.begin)}</span>
                  <span class="hidden cc2-md:block">{took(task)}</span>
                  <span class="hidden cc2-md:block">
                    <Tag tone={tone}>{t(label)}</Tag>
                  </span>
                  <div class="justify-self-end">
                    <Video task={task} idle={v.idle} busy={h.generating} />
                  </div>
                </div>
              )
            })}
            {h.tasks.length > shown && (
              <Button wide class="mt-3" onClick={() => setShown(n => n + STEP)}>
                {tpl('history.show_more_n_of_total', {
                  n: Math.min(STEP, h.tasks.length - shown),
                  total: h.tasks.length,
                })}
              </Button>
            )}
          </div>
        )}
      </Card>
    </Page>
  )
}
