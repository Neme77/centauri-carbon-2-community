import { useEffect, useRef, useState } from 'preact/hooks'
import { Download, Film, Play, RefreshCw, Trash2 } from 'lucide-preact'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Pagination, PAGE_SIZE, pageIndex } from '@/components/pagination'
import { Button } from '@/components/ui/button'
import { Tag } from '@/components/ui/badge'
import { FilePicture } from '@/components/file-picture'
import { errText, notify } from '@/lib/api'
import { type FileEntry, fileTag, listFiles } from '@/lib/files'
import { ask } from '@/lib/confirm'
import { type Key, t, tpl } from '@/lib/i18n'
import { duration, fileSize } from '@/lib/format'
import {
  history,
  refreshHistory,
  renderTimelapse,
  requestHistory,
  deleteHistory,
  type Task,
  timelapseUrl,
} from '@/lib/history'
import { printerError } from '@/lib/machine'
import { usePoll } from '@/lib/poll'
import { printer, view } from '@/lib/state'
import { startFile } from '@/pages/print-dialog'

const I = { size: 16, strokeWidth: 1 }
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
  const [page, setPage] = useState(0)
  const currentPage = pageIndex(page, h.tasks.length)
  useEffect(() => setPage(currentPage), [currentPage])
  const [failure, setFailure] = useState('')
  // Reads come from CC2 Control's memory; only Refresh and opening the page ask the printer.
  const [deleting, setDeleting] = useState(false)
  const locked = deleting || h.deleting || h.generating || h.pending
  const completed = h.tasks.filter(task => task.status >= 1 && task.status <= 3)
  usePoll(refreshHistory, h.pending || h.generating || h.deleting ? 2000 : 30000)
  const remove = async (tasks: Task[]) => {
    if (!tasks.length || !(await ask(tpl('history.confirm_delete', { n: tasks.length }), true))) return
    setDeleting(true)
    try {
      await deleteHistory(tasks.map(task => task.id))
      notify(t('history.delete_requested'))
      await refreshHistory()
    } catch (e) {
      notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
    } finally {
      setDeleting(false)
    }
  }
  // The printer's history names a file, not where it is. The file list says whether the file is still there:
  // for its picture and for printing it again. Read when the page opens and with Refresh, never polled.
  const [files, setFiles] = useState<Map<string, FileEntry> | null>(null)
  const listing = useRef<AbortController | null>(null)
  const readFiles = async () => {
    listing.current?.abort()
    const controller = new AbortController()
    listing.current = controller
    try {
      const { entries } = await listFiles(controller.signal)
      if (controller.signal.aborted) return
      // A file in the root of a storage before one of the same name in a folder; the internal memory first.
      const named = new Map<string, FileEntry>()
      for (const entry of entries) if (!named.has(entry.file.path)) named.set(entry.file.path, entry)
      for (const entry of entries) {
        const name = String(entry.file.path).split('/').pop() || ''
        if (!named.has(name)) named.set(name, entry)
      }
      setFiles(named)
    } catch {
      /* the list did not come: nothing is known about the files, so nothing is said about them */
    }
  }
  const refresh = async () => {
    void readFiles()
    try {
      setPage(0)
      setFailure('')
      await requestHistory()
    } catch (e) {
      setFailure(errText(e))
    }
    void refreshHistory()
  }
  useEffect(() => {
    void refresh()
    return () => listing.current?.abort()
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
  // A narrow window gives the picture and the name the first line and the actions the second.
  const cols =
    'grid grid-cols-[48px_minmax(0,1fr)] items-center gap-x-3 gap-y-1.5 border-b border-edge px-1 py-2 cc2-md:grid-cols-[48px_minmax(0,2.2fr)_1fr_.7fr_.8fr_15.5rem]'
  return (
    <Page title="common.history" sub="history.jobs_recorded_by_the_printer_and">
      <Card>
        <CardHead
          icon="file"
          title="history.print_jobs"
          end={
            <div class="flex flex-wrap justify-end gap-2">
              <Button
                class="min-h-8 px-2 text-xs"
                disabled={!v.idle || locked || !completed.length}
                onClick={() => remove(completed)}
              >
                <Trash2 {...I} />
                {t('history.clear_history')}
              </Button>
              <Button class="min-h-8 px-2 text-xs text-fg" disabled={h.pending} onClick={refresh}>
                <RefreshCw {...I} />
                {t('history.refresh')}
              </Button>
            </div>
          }
        />
        {status && <p class="py-3 text-muted">{status}</p>}
        {h.tasks.length > 0 && (
          <div class="cc2-history-list">
            <div class={`${cols} hidden text-xs text-muted cc2-md:grid`}>
              <span />
              <span>{t('common.name')}</span>
              <span>{t('history.started')}</span>
              <span>{t('history.duration')}</span>
              <span>{t('history.result')}</span>
              <span class="justify-self-end">{t('history.time_lapse')}</span>
            </div>
            {h.tasks.slice(currentPage * PAGE_SIZE, (currentPage + 1) * PAGE_SIZE).map(task => {
              const [label, tone] = RESULT[task.status] ?? ['history.unknown', 'default']
              const file = files?.get(task.name)
              return (
                <div key={task.id} class={cols} data-history-task={task.id}>
                  <FilePicture
                    storage={file?.storage || 'internal'}
                    path={file ? file.file.path : ''}
                    tag={file ? fileTag(file.file) : ''}
                    row
                  />
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
                  <div class="col-span-2 flex items-center justify-end gap-2 cc2-md:col-span-1">
                    <Video task={task} idle={v.idle} busy={locked} />
                    {/* The print dialog of the Files page, for the file of this job while the printer still has it. */}
                    <Button
                      class="px-2"
                      data-history-print
                      title={file ? t('files.print') : files ? t('history.file_gone') : undefined}
                      aria-label={tpl('history.print_named', { name: task.name })}
                      disabled={!file}
                      onClick={() => file && startFile(file.storage, file.file.path)}
                    >
                      <Play {...I} />
                    </Button>
                    <Button
                      class="px-2"
                      title={t('history.delete_entry')}
                      aria-label={tpl('history.delete_named', { name: task.name })}
                      disabled={!v.idle || locked || task.status < 1 || task.status > 3}
                      onClick={() => remove([task])}
                    >
                      <Trash2 {...I} />
                    </Button>
                  </div>
                </div>
              )
            })}
            <Pagination page={currentPage} total={h.tasks.length} onChange={setPage} />
          </div>
        )}
      </Card>
    </Page>
  )
}
