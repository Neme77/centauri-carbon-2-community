import { useEffect, useState } from 'preact/hooks'
import { Button } from '@/components/ui/button'
import { Dialog, DialogTitle } from '@/components/ui/dialog'
import { FeedChoice, isSlot, isStock, stockBrands, stockOf } from '@/components/feed-choice'
import { JobFilament } from '@/components/queue-next'
import { errText, notify } from '@/lib/api'
import { canvas, refreshCanvas } from '@/lib/canvas'
import { t, tpl } from '@/lib/i18n'
import {
  filamentKnown,
  jobFilaments,
  type QueueItem,
  refreshQueue,
  setQueuedFilaments,
  type ToolFilament,
} from '@/lib/queue'
import { refreshSpools, spools } from '@/lib/spools'

// Another filament for a queued job: per tool, the filament a Canvas slot holds now, or one of the spool
// inventory that is in no slot (the job then waits for it). A tool left alone keeps its filament.
export const JobFilamentDialog = ({ item, onClose }: { item: QueueItem; onClose: () => void }) => {
  const trays = canvas.use().model?.trays || []
  const stock = stockBrands(spools.use().data)
  const [choice, setChoice] = useState<Record<number, string>>({})
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  // What the dialog offers is read once when it opens: the slots as they are labelled now and the inventory.
  useEffect(() => {
    void refreshCanvas()
    void refreshSpools()
  }, [])
  const tools = jobFilaments(item)
  // A tool counts once its choice is whole: a slot, or a filament of the inventory down to its colour.
  const changed = tools.filter(f => isSlot(choice[f.tool]) || stockOf(stock, choice[f.tool]))
  const unfinished = tools.some(f => isStock(choice[f.tool]) && !stockOf(stock, choice[f.tool]))
  const save = async (e: Event) => {
    e.preventDefault()
    if (busy || unfinished || !changed.length) return
    setBusy(true)
    setError('')
    try {
      const mapping = changed
        .filter(f => isSlot(choice[f.tool]))
        .map(f => `${f.tool}:${choice[f.tool]}`)
        .join(',')
      const free: ToolFilament[] = changed.flatMap(f => {
        const s = stockOf(stock, choice[f.tool])
        return s ? [{ tool: f.tool, type: s.type, name: s.name, color: s.color }] : []
      })
      await setQueuedFilaments(item.id, mapping, free)
      notify(t('queue.filament_changed'))
      await refreshQueue()
      onClose()
    } catch (err) {
      setError(errText(err))
      setBusy(false)
    }
  }
  return (
    <Dialog onClose={onClose} locked={busy} width={820} label={t('queue.filament_title')}>
      <form onSubmit={save} data-queue-filament-dialog>
        <DialogTitle class="mb-1">{t('queue.filament_title')}</DialogTitle>
        <div class="mb-3 text-muted [overflow-wrap:anywhere]">{item.path}</div>
        <p class="text-xs text-muted">{t('queue.filament_hint')}</p>
        {tools.map(f => (
          <fieldset key={f.tool} class="my-4 min-w-0 rounded-lg border border-edge p-3">
            <legend class="px-1.5 font-semibold">{tpl('print.filament_tool_n', { n: f.tool })}</legend>
            {filamentKnown(f) && (
              <div class="mb-3 text-xs">
                <JobFilament f={f} />
              </div>
            )}
            <FeedChoice
              tool={f.tool}
              name={`queue-tool-${f.tool}`}
              trays={trays}
              value={choice[f.tool] || ''}
              onChange={v => setChoice(c => ({ ...c, [f.tool]: v }))}
              disabled={busy}
              stock={stock}
            />
          </fieldset>
        ))}
        {error && (
          <div class="my-2 text-xs text-red" role="alert">
            {tpl('queue.failed', { error })}
          </div>
        )}
        <div class="mt-5 flex justify-end gap-2">
          <Button disabled={busy} onClick={onClose}>
            {t('common.cancel')}
          </Button>
          <Button
            type="submit"
            variant="primary"
            disabled={busy || unfinished || !changed.length}
            data-queue-filament-save
          >
            {t('filament.save')}
          </Button>
        </div>
      </form>
    </Dialog>
  )
}
