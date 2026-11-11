import { useState } from 'preact/hooks'
import { Play } from 'lucide-preact'
import { Button } from '@/components/ui/button'
import { Icon } from '@/components/icons'
import { errText, notify } from '@/lib/api'
import { duration } from '@/lib/format'
import { t, tpl } from '@/lib/i18n'
import { usePoll } from '@/lib/poll'
import { Tag } from '@/components/ui/badge'
import { holdQueue, openJobs, queue, queuedCopiesFor, queueNext, refreshQueue, startQueued } from '@/lib/queue'
import { printer, refreshPrinter, view } from '@/lib/state'

// Marks a file of the Files page that still has copies in the queue.
export const QueuedChip = ({ storage, path }: { storage: string; path: string }) => {
  usePoll(refreshQueue, 10000)
  const n = queuedCopiesFor(queue.use().data, storage, path)
  if (!n) return null
  return (
    <Tag class="border-cyan text-cyan" data-queued-chip>
      {tpl('queue.in_queue_n', { n })}
    </Tag>
  )
}

// "Next in queue" on the Dashboard's current-job card: the quick start once the bed is clear.
export const QueueNext = () => {
  usePoll(refreshQueue, 5000)
  const q = queue.use().data
  const { data, ok } = printer.use()
  const v = view(data)
  const [busy, setBusy] = useState(false)
  const next = queueNext(q)
  if (!q?.available || !next || v.active || q.active) return null
  const more = openJobs(q).length - 1
  const start = async () => {
    setBusy(true)
    try {
      await startQueued(next.id)
      notify(tpl('queue.started', { k: next.done + 1, n: next.copies, name: next.path }))
    } catch (e) {
      notify(tpl('queue.failed', { error: errText(e) }), 'error')
    }
    await Promise.all([refreshQueue(), refreshPrinter()])
    setBusy(false)
  }
  const hold = async () => {
    setBusy(true)
    try {
      await holdQueue()
    } catch (e) {
      notify(tpl('queue.failed', { error: errText(e) }), 'error')
    }
    await refreshQueue()
    setBusy(false)
  }
  return (
    <div class="mb-3 rounded-lg border border-cyan/60 bg-field p-3" data-dashboard-queue>
      <div class="mb-1.5 flex items-center gap-2">
        <Icon n="queue" class="text-cyan" />
        <h3 class="text-base font-semibold">{t('queue.next_in_queue')}</h3>
        <a href="#queue" class="ml-auto text-xs text-cyan underline underline-offset-2">
          {t('queue.open_queue')}
        </a>
      </div>
      <div class="flex flex-wrap items-center gap-3">
        <div class="min-w-0 flex-1 basis-48">
          <div class="font-semibold [overflow-wrap:anywhere]">{next.path}</div>
          <div class="text-xs text-muted">
            {tpl('queue.copy_k_of_n', { k: next.done + 1, n: next.copies })}
            {next.estimated_seconds !== null && ` · ≈ ${duration(next.estimated_seconds)}`}
            {more > 0 && ` · ${tpl('queue.more_jobs_n', { n: more })}`}
          </div>
        </div>
        <Button
          variant="primary"
          class="min-h-11"
          disabled={busy || !ok || !data?.connected || !v.idle || !next.present}
          onClick={start}
          data-dashboard-queue-start
        >
          <Play size={18} strokeWidth={1} />
          {t('queue.start_next')}
        </Button>
      </div>
      {q.auto_in !== null && (
        <div class="mt-2 flex items-center gap-2 text-xs text-amber" data-dashboard-queue-auto>
          {tpl('queue.auto_in_n', { n: q.auto_in })}
          <Button class="min-h-8 px-2 text-xs" disabled={busy} onClick={hold}>
            {t('queue.hold')}
          </Button>
        </div>
      )}
      <div class="mt-2 text-xs text-muted">{t('queue.remove_part_hint')}</div>
    </div>
  )
}
