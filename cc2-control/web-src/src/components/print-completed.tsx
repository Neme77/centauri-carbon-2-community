import { useEffect, useRef, useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { CompletionTracker, replayable, type Completion } from '@/lib/print-completion'
import { ls } from '@/lib/store'
import { printer, view } from '@/lib/state'
import { t, tpl } from '@/lib/i18n'
import { errText, notify } from '@/lib/api'
import { duration } from '@/lib/format'
import { holdQueue, queue, queueNext, refreshQueue, startQueued } from '@/lib/queue'
import { usePoll } from '@/lib/poll'
import { startFile } from '@/pages/print-dialog'
const storageKey = 'cc2-completed-print-seen'
const readSeen = () => {
  try {
    const value = JSON.parse(ls.get(storageKey) || '[]')
    return new Set<string>(Array.isArray(value) ? value.filter(v => typeof v === 'string').slice(-32) : [])
  } catch {
    return new Set<string>()
  }
}
export const PrintCompleted = () => {
  const { data, ok } = printer.use()
  const seen = useRef(readSeen()),
    tracker = useRef<CompletionTracker | null>(null)
  if (!tracker.current) tracker.current = new CompletionTracker(seen.current)
  const ending = useRef<string | null>(null)
  const [notice, setNotice] = useState<Completion | null>(null),
    [busy, setBusy] = useState(false)
  useEffect(() => {
    const age = Number(data?.last_message_age)
    if (ok && data?.connected && Number.isFinite(age) && age >= 0 && age <= 15) {
      const state = String(data?.print?.native?.state || data?.print?.state || '')
      if (Number(data?.machine?.status) === 2 && /^(printing|paused)$/.test(state))
        ending.current = data?.print?.native?.filename || data?.print?.filename || null
      else if (/^(cancelled|error)$/.test(state) && ending.current)
        notify(tpl('app.print_ended', { name: ending.current }))
      if (/^(complete|cancelled|error)$/.test(state)) ending.current = null
    }
    const next = tracker.current?.observe(data, ok)
    if (next) setNotice(next)
    else if (ok && data?.connected && Number(data?.machine?.status) === 2) setNotice(null)
  }, [data, ok])
  const dismiss = () => {
    if (notice) {
      seen.current.add(notice.key)
      seen.current.add(notice.alias)
      ls.set(storageKey, JSON.stringify([...seen.current].slice(-32)))
    }
    setNotice(null)
  }
  // The queue's next copy is one tap away once the part is off the bed.
  const q = queue.use().data
  // The notice is where an automatic countdown must be visible: poll fast while it shows (otherwise
  // this only joins the slower poll of the layout).
  usePoll(refreshQueue, notice ? 2000 : 30000)
  const next = notice && q?.available && !q.active ? queueNext(q) : null
  const autoIn = next && q ? q.auto_in : null
  const hold = async () => {
    setBusy(true)
    try {
      await holdQueue()
    } catch (e) {
      notify(tpl('queue.failed', { error: errText(e) }), 'error')
    } finally {
      setBusy(false)
      void refreshQueue()
    }
  }
  const startNext = async () => {
    if (!next) return
    setBusy(true)
    try {
      await startQueued(next.id)
      notify(tpl('queue.started', { k: next.done + 1, n: next.copies, name: next.path }))
      dismiss()
    } catch (e) {
      notify(tpl('queue.failed', { error: errText(e) }), 'error')
    } finally {
      setBusy(false)
      void refreshQueue()
    }
  }
  if (!notice) return null
  const ready =
    ok &&
    data?.connected &&
    Number.isFinite(Number(data?.last_message_age)) &&
    Number(data?.last_message_age) >= 0 &&
    Number(data?.last_message_age) <= 15 &&
    view(data).idle
  return (
    <Card
      role="region"
      aria-label={t('completed.title')}
      data-print-completed
      class="fixed right-3 bottom-3 z-40 w-[min(420px,calc(100vw-24px))] max-h-[65vh] overflow-auto shadow-2xl"
    >
      <CardHead icon="file" title={notice.live ? 'completed.title' : 'completed.latest'} />
      <p class="break-words font-semibold">{notice.path}</p>
      <p class="mt-2 text-muted">
        {notice.seconds > 0 ? duration(notice.seconds) : '—'} · {t('completed.hint')}
      </p>
      {next && (
        <div class="mt-3 rounded-md border border-edge bg-field p-2.5" data-completed-queue>
          <div class="text-xs text-muted">{t('queue.next_in_queue')}</div>
          <div class="font-semibold [overflow-wrap:anywhere]">
            {next.path} — {tpl('queue.copy_k_of_n', { k: next.done + 1, n: next.copies })}
          </div>
          {autoIn === null ? (
            <div class="text-xs text-muted">{t('queue.remove_part_hint')}</div>
          ) : (
            <div class="mt-1 flex flex-wrap items-center gap-2 text-xs text-amber" data-completed-queue-auto>
              {tpl('queue.auto_in_n', { n: autoIn })}
              <Button class="min-h-8 px-2 text-xs" disabled={busy} onClick={hold}>
                {t('queue.hold')}
              </Button>
            </div>
          )}
        </div>
      )}
      <div class="mt-3 flex flex-wrap gap-2">
        <Button onClick={dismiss}>{t('completed.acknowledge')}</Button>
        {next && (
          <Button
            variant="primary"
            disabled={busy || !ready || !next.present}
            onClick={startNext}
            data-completed-queue-start
          >
            {t('queue.start_next')}
          </Button>
        )}
        <Button
          disabled={busy || !ready || !replayable(notice.path)}
          onClick={async () => {
            setBusy(true)
            try {
              await startFile('internal', notice.path)
            } finally {
              setBusy(false)
            }
          }}
        >
          {t('completed.again')}
        </Button>
      </div>
    </Card>
  )
}
