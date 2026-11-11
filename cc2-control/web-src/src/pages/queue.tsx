import { useEffect, useLayoutEffect, useRef, useState } from 'preact/hooks'
import {
  ChevronDown,
  ChevronRight,
  ChevronUp,
  FileText,
  Minus,
  Palette,
  Play,
  Plus,
  SkipForward,
  Trash2,
} from 'lucide-preact'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Tag } from '@/components/ui/badge'
import { Input, Switch } from '@/components/ui/field'
import { Progress } from '@/components/shared'
import { Icon } from '@/components/icons'
import { errText, notify, request } from '@/lib/api'
import { ask } from '@/lib/confirm'
import { type Key, t, tpl } from '@/lib/i18n'
import { duration } from '@/lib/format'
import { usePoll } from '@/lib/poll'
import { openPage, printer, refreshPrinter, view } from '@/lib/state'
import { canvas, refreshCanvas } from '@/lib/canvas'
import { nozzleName, type PlateLibrary, plates, refreshPlates } from '@/lib/plates'
import { stateText } from '@/lib/machine'
import { readMeshReadiness } from '@/pages/print-dialog'
import { JobFilament, JobFilaments, missingNote } from '@/components/queue-next'
import { JobFilamentDialog } from '@/components/queue-filament'
import {
  clearFinished,
  copiesLeft,
  abandonQueued,
  type FilamentLabel,
  filamentKnown,
  finishedJobs,
  holdQueue,
  jobFilaments,
  jobPicture,
  missingFilaments,
  openJobs,
  type Queue as QueueData,
  type QueueItem,
  type QueuePolicy,
  queue,
  queueActive,
  queueNext,
  queueTotals,
  refreshQueue,
  removeQueued,
  reorderQueued,
  setQueuePolicy,
  setQueuedCopies,
  startBlocked,
  startQueued,
} from '@/lib/queue'

const I = { size: 16, strokeWidth: 1 }
type Tone = 'ok' | 'warn' | 'bad' | 'info'
// `filament`: the job's filament a check is about, shown after its text.
type Check = { tone: Tone; text: string; filament?: FilamentLabel | null }
type Filament = { tool: number; color: string; material: string }
type MeshSides = { A: boolean | null; B: boolean | null }

// Fixed caches: the head of the queue changes rarely, the page polls often.
const inspected = new Map<string, Filament[]>()
const where = (storage: string) => (storage === 'usb' ? t('files.usb_drive') : t('files.internal_memory'))
const fileKey = (i: QueueItem) => `${i.storage}\n${i.path}`

// Everything the operator would otherwise check in the print dialog, read from what is already known.
function preflight(
  item: QueueItem,
  d: any,
  ok: boolean,
  trays: any[],
  filaments: Filament[] | null,
  mesh: MeshSides | null,
  lib: PlateLibrary | null
): Check[] {
  const checks: Check[] = []
  checks.push(
    item.present
      ? { tone: 'ok', text: tpl('queue.check_file_ok', { where: where(item.storage) }) }
      : { tone: 'bad', text: tpl('queue.check_file_missing', { where: where(item.storage) }) }
  )
  const feeds = jobFilaments(item)
  if (!feeds.length) checks.push({ tone: 'info', text: t('queue.check_external') })
  // A filament of the job that is in no slot stops the start: said once per filament.
  for (const f of missingFilaments(item))
    checks.push({ tone: 'bad', text: missingNote(f), filament: filamentKnown(f) ? f : null })
  // Every other tool prints from the slot that holds its filament now: that slot is checked against the file.
  for (const { tool, use } of feeds) {
    if (use === null) continue
    const tray = trays.find(x => x && Number(x.tray_id) === use)
    const label = (tray && (tray.filament_name || tray.filament_type)) || ''
    const type = String(tray?.filament_type || tray?.filament_name || '')
    const wanted = filaments?.find(f => f.tool === tool)?.material || ''
    const n = use + 1
    if (!label) checks.push({ tone: 'warn', text: tpl('queue.check_slot_unknown', { n }) })
    else if (!wanted) checks.push({ tone: 'ok', text: tpl('queue.check_slot', { n, filament: label }) })
    else if (
      type.toLowerCase().includes(wanted.toLowerCase()) ||
      wanted.toLowerCase().includes(type.toLowerCase().split(' ')[0])
    )
      checks.push({ tone: 'ok', text: tpl('queue.check_slot_ok', { n, filament: label }) })
    else checks.push({ tone: 'warn', text: tpl('queue.check_slot_differs', { n, filament: label, wanted }) })
  }
  if (item.leveling !== 'saved') checks.push({ tone: 'info', text: tpl('queue.check_calibrate', { side: item.side }) })
  else if (!mesh || mesh[item.side] === null)
    checks.push({ tone: 'info', text: tpl('queue.check_mesh_unknown', { side: item.side }) })
  else if (mesh[item.side]) checks.push({ tone: 'ok', text: tpl('queue.check_mesh_saved', { side: item.side }) })
  else checks.push({ tone: 'warn', text: tpl('queue.check_mesh_missing', { side: item.side }) })
  if (item.nozzle && lib?.nozzles.length) {
    const wanted = nozzleName(lib, item.nozzle) || item.nozzle
    if (lib.nozzle === item.nozzle) checks.push({ tone: 'ok', text: tpl('queue.check_nozzle_ok', { name: wanted }) })
    else
      checks.push({
        tone: 'warn',
        text: tpl('queue.check_nozzle_differs', {
          installed: nozzleName(lib, lib.nozzle) || t('common.not_set'),
          wanted,
        }),
      })
  }
  const v = view(d)
  if (!ok || !d?.connected) checks.push({ tone: 'bad', text: t('queue.check_printer_offline') })
  else if (v.idle) {
    const bed = Number(d?.heater_bed?.temperature)
    checks.push({
      tone: 'ok',
      text: tpl('queue.check_printer_idle', { temp: Number.isFinite(bed) ? Math.round(bed) : '—' }),
    })
  } else checks.push({ tone: 'warn', text: tpl('queue.check_printer_busy', { state: stateText(v) }) })
  return checks
}

const CheckList = ({ checks }: { checks: Check[] }) => (
  <ul class="grid gap-1.5" data-preflight>
    {checks.map((c, i) => (
      <li key={i} class="flex items-start gap-2" data-tone={c.tone}>
        <i
          class={`mt-1.5 inline-block size-2.5 shrink-0 rounded-full ${
            c.tone === 'ok' ? 'bg-green' : c.tone === 'warn' ? 'bg-amber' : c.tone === 'bad' ? 'bg-red' : 'bg-cyan'
          }`}
        />
        <div class="min-w-0">
          <span class={c.tone === 'warn' ? 'text-amber' : c.tone === 'bad' ? 'text-red' : ''}>{c.text}</span>
          {c.filament && (
            <>
              {' '}
              <JobFilament f={c.filament} />
            </>
          )}
        </div>
      </li>
    ))}
  </ul>
)

// The job's print settings, as the dialog saved them, and the filaments it is bound to, with the way to
// give a tool another one.
const Settings = ({ item, lib }: { item: QueueItem; lib: PlateLibrary | null }) => {
  const [edit, setEdit] = useState(false)
  const more: string[] = []
  if (item.leveling !== 'saved') more.push(t('queue.calibration'))
  if (item.timelapse) more.push(t('queue.timelapse'))
  if (item.nozzle) more.push(tpl('queue.nozzle_x', { name: nozzleName(lib, item.nozzle) || item.nozzle }))
  return (
    <div class="flex flex-wrap items-center gap-1.5">
      <Tag>{tpl('queue.side_n', { side: item.side })}</Tag>
      <JobFilaments item={item} />
      {jobFilaments(item).length > 0 && item.state !== 'done' && (
        <Button
          variant="ghost"
          class="min-h-7 px-1.5"
          aria-label={t('queue.filament_change')}
          title={t('queue.filament_change')}
          onClick={() => setEdit(true)}
          data-queue-filament-edit
        >
          <Palette size={14} strokeWidth={1} />
        </Button>
      )}
      {more.map(c => (
        <Tag key={c}>{c}</Tag>
      ))}
      {edit && <JobFilamentDialog item={item} onClose={() => setEdit(false)} />}
    </div>
  )
}

// The picture the slicer put in the job's G-code, or a file icon when it has none: large on the card of the
// current job, small in a row of the list (where the file name next to it says what it shows).
const Thumb = ({ item, row }: { item: QueueItem; row?: boolean }) => {
  const [url, setUrl] = useState('')
  useEffect(() => {
    let dead = false
    setUrl('')
    jobPicture(item).then(made => !dead && setUrl(made))
    return () => {
      dead = true
    }
  }, [item.id, fileKey(item)])
  return (
    <div
      data-queue-picture={url ? 'shown' : 'none'}
      class={`grid shrink-0 place-items-center overflow-hidden border border-edge bg-well ${
        row ? 'size-12 rounded-md' : 'size-28 rounded-lg'
      }`}
    >
      {url ? (
        <img
          src={url}
          alt={row ? '' : t('files.g_code_model_preview')}
          draggable={false}
          class={`size-full object-contain ${row ? 'p-0.5' : 'p-1.5'}`}
        />
      ) : (
        <FileText size={row ? 22 : 40} strokeWidth={1} class="text-edge" />
      )}
    </div>
  )
}

const copyText = (item: QueueItem) =>
  tpl('queue.copy_k_of_n', { k: Math.min(item.done + 1, item.copies), n: item.copies })

type Done = { key: Key; vars?: Record<string, string | number> }
const act = async (fn: () => Promise<unknown>, done?: Done) => {
  try {
    await fn()
    if (done) notify(tpl(done.key, done.vars ?? {}))
  } catch (e) {
    notify(tpl('queue.failed', { error: errText(e) }), 'error')
  }
  await refreshQueue()
}

// The job that prints now, or the one that starts next.
const Hero = ({
  q,
  item,
  active,
  filaments,
  mesh,
  lib,
}: {
  q: QueueData
  item: QueueItem
  active: boolean
  filaments: Filament[] | null
  mesh: MeshSides | null
  lib: PlateLibrary | null
}) => {
  const { data: d, ok } = printer.use()
  const v = view(d)
  const trays = canvas.use().model?.trays || []
  const [busy, setBusy] = useState(false)
  const checks = active ? [] : preflight(item, d, ok, trays, filaments, mesh, lib)
  const canStart = !active && item.present && !startBlocked(item) && v.idle && ok && Boolean(d?.connected) && !q.active
  const start = async () => {
    setBusy(true)
    await act(() => startQueued(item.id), {
      key: 'queue.started',
      vars: { k: item.done + 1, n: item.copies, name: item.path },
    })
    await refreshPrinter()
    setBusy(false)
  }
  const skip = () => act(() => reorderQueued(item.id, Math.max(0, q.items.length - 1)))
  const guarded = async (fn: () => Promise<unknown>) => {
    setBusy(true)
    await act(fn)
    setBusy(false)
  }
  return (
    <Card class={active ? '' : 'border-cyan/60'} data-queue-hero={active ? 'active' : 'next'}>
      <CardHead
        icon={active ? 'file' : 'queue'}
        title={active ? 'queue.now_printing' : 'queue.next_job'}
        end={active ? stateText(v) : q.printer_idle ? t('queue.printer_ready') : stateText(v)}
      />
      <div class="flex flex-wrap items-start gap-3.5">
        <Thumb item={item} />
        <div class="grid min-w-0 flex-1 basis-64 gap-2">
          <h3 class="text-base font-semibold [overflow-wrap:anywhere]">{item.path}</h3>
          <div class="text-muted">
            <strong class="font-semibold text-cyan">{copyText(item)}</strong>
            {item.estimated_seconds !== null && ` · ≈ ${duration(item.estimated_seconds)}`}
            {item.filament_grams !== null && ` · ${item.filament_grams.toFixed(1)} g`}
          </div>
          <Settings item={item} lib={lib} />
          {active ? (
            <div class="mt-1 grid gap-2">
              <Progress pct={v.progress} />
              <div class="text-xs text-muted">
                {t('common.elapsed')} {v.elapsedText} · {tpl('common.remaining_ends_time', { time: v.finishText })}{' '}
                {v.remainingText}
              </div>
            </div>
          ) : (
            <div class="mt-1">
              <h3 class="mb-1.5 text-base font-semibold">{t('queue.preflight')}</h3>
              <CheckList checks={checks} />
            </div>
          )}
        </div>
      </div>
      <div class="mt-3.5 flex flex-wrap items-center gap-2 border-t border-edge pt-3">
        <Button
          variant="primary"
          class="min-h-11 px-5 text-base"
          disabled={!canStart || busy}
          onClick={start}
          data-queue-start
        >
          <Play size={18} strokeWidth={1} />
          {t(active ? 'queue.start_next' : 'print.start_print')}
        </Button>
        {active ? (
          <>
            <Button class="min-h-11" onClick={() => openPage('job')}>
              {t('queue.open_job')}
            </Button>
            {item.state === 'starting' && (
              <Button
                class="min-h-11"
                disabled={busy}
                onClick={() => guarded(() => abandonQueued(item.id))}
                data-queue-abandon
              >
                {t('queue.abandon')}
              </Button>
            )}
          </>
        ) : (
          <Button class="min-h-11" onClick={skip} disabled={busy || openJobs(q).length < 2} data-queue-skip>
            <SkipForward {...I} />
            {t('queue.skip')}
          </Button>
        )}
        {!active && q.auto_in !== null && (
          <span class="flex items-center gap-2 text-xs text-amber" data-queue-auto>
            {tpl('queue.auto_in_n', { n: q.auto_in })}
            <Button class="min-h-8 px-2 text-xs" disabled={busy} onClick={() => guarded(() => holdQueue())}>
              {t('queue.hold')}
            </Button>
          </span>
        )}
        <span class="text-xs text-muted">{t(active ? 'queue.start_locked' : 'queue.remove_part_hint')}</span>
      </div>
    </Card>
  )
}

const Policy = ({ policy }: { policy: QueuePolicy }) => {
  const [delay, setDelay] = useState(String(policy.delay))
  useEffect(() => setDelay(String(policy.delay)), [policy.delay])
  const save = (p: QueuePolicy) => act(() => setQueuePolicy(p))
  const commitDelay = () => {
    const n = Number(delay)
    if (Number.isInteger(n) && n >= 10 && n <= 3600 && n !== policy.delay) save({ ...policy, delay: n })
    else setDelay(String(policy.delay))
  }
  const label = 'flex cursor-pointer items-start gap-2'
  return (
    <Card data-queue-policy>
      <CardHead icon="settings" title="queue.after_print" />
      <div class="grid gap-3">
        <label class={label}>
          <input
            type="radio"
            name="queue-after"
            class="mt-1"
            checked={policy.after === 'wait'}
            onChange={() => save({ ...policy, after: 'wait' })}
          />
          <span>
            <strong class="font-semibold">{t('queue.policy_wait')}</strong>
            <br />
            <span class="text-xs text-muted">{t('queue.policy_wait_hint')}</span>
          </span>
        </label>
        <label class={label}>
          <input
            type="radio"
            name="queue-after"
            class="mt-1"
            checked={policy.after === 'auto'}
            onChange={() => save({ ...policy, after: 'auto' })}
          />
          <span class="min-w-0">
            <strong class="font-semibold">{t('queue.policy_auto')}</strong>{' '}
            <span class="text-muted">{t('queue.policy_auto_after')}</span>{' '}
            <Input
              type="number"
              min={10}
              max={3600}
              class="inline-block w-20"
              aria-label={t('queue.policy_delay_label')}
              value={delay}
              onInput={e => setDelay(e.currentTarget.value)}
              onBlur={commitDelay}
              onKeyDown={e => e.key === 'Enter' && commitDelay()}
            />{' '}
            <span class="text-muted">{t('queue.seconds_short')}</span>
            <br />
            <span class="text-xs text-amber">{t('queue.policy_auto_warning')}</span>
          </span>
        </label>
        <div class="flex items-center justify-between gap-2 border-t border-edge pt-2.5">
          <span>{t('queue.remove_done')}</span>
          <Switch
            on={policy.remove_done}
            label={t('queue.remove_done')}
            onClick={() => save({ ...policy, remove_done: !policy.remove_done })}
          />
        </div>
      </div>
    </Card>
  )
}

const Totals = ({ q }: { q: QueueData }) => {
  const totals = queueTotals(q)
  const cell = (label: Key, value: string | number) => (
    <div class="rounded-md border border-edge px-2.5 py-2">
      <div class="text-xs text-muted">{t(label)}</div>
      <div class="text-xl font-semibold">{value}</div>
    </div>
  )
  return (
    <Card data-queue-totals>
      <CardHead icon="history" title="queue.totals" />
      <div class="grid grid-cols-3 gap-2">
        {cell('queue.jobs', totals.jobs)}
        {cell('queue.copies_ahead', totals.copies)}
        {cell('queue.time', totals.copies ? `${totals.unknown ? '> ' : ''}${duration(totals.seconds)}` : '—')}
      </div>
    </Card>
  )
}

const stateTag = (item: QueueItem, q: QueueData) => {
  if (item.state === 'printing') return <Tag tone="ok">{t('queue.printing_tag')}</Tag>
  if (item.state === 'starting') return <Tag tone="ok">{t('queue.starting_tag')}</Tag>
  if (!item.present) return <Tag tone="warning">{t('queue.file_missing')}</Tag>
  if (q.next === item.id) return <Tag class="border-cyan text-cyan">{t('queue.next_tag')}</Tag>
  return null
}

const lastResult = (item: QueueItem) =>
  item.last_result === 'stopped'
    ? t('queue.stopped')
    : item.last_result === 'not_started'
      ? t('queue.not_started')
      : item.last_result === 'auto_failed'
        ? t('queue.auto_failed')
        : item.last_result === 'auto_material'
          ? t('queue.auto_material')
          : ''

// What the picture of a row does with the pointer while the row can be dragged to another place.
type Grip = Record<
  'onPointerDown' | 'onPointerMove' | 'onPointerUp' | 'onPointerCancel' | 'onLostPointerCapture',
  (e: PointerEvent) => void
>

// `grip`: the row can be dragged by its picture. `lifted`: it is the one being dragged (the pointer moves it,
// not this component). `shift`: the pixels a row moves aside for the dragged one, null when nothing is dragged.
const Row = ({
  item,
  index,
  q,
  lib,
  grip,
  lifted,
  shift,
}: {
  item: QueueItem
  index: number
  q: QueueData
  lib: PlateLibrary | null
  grip: Grip | null
  lifted: boolean
  shift: number | null
}) => {
  const jobs = openJobs(q)
  const running = item.id === q.active
  // One change at a time per row: the next click waits for the refreshed queue, so it never repeats a stale value.
  const [busy, setBusy] = useState(false)
  const run = async (fn: () => Promise<unknown>) => {
    if (busy) return
    setBusy(true)
    try {
      await act(fn)
    } finally {
      setBusy(false)
    }
  }
  const copies = (n: number) => run(() => setQueuedCopies(item.id, n))
  const remove = async () => {
    if (await ask(tpl('queue.remove_confirm', { name: item.path }), true)) await run(() => removeQueued(item.id))
  }
  const move = (to: number) => run(() => reorderQueued(item.id, q.items.indexOf(jobs[to])))
  const note = lastResult(item)
  return (
    <div
      data-queue-item={item.id}
      data-queue-drag={lifted ? 'lifted' : undefined}
      style={!lifted && shift !== null ? { transform: `translateY(${shift}px)` } : undefined}
      class={`grid grid-cols-[48px_minmax(0,1fr)_auto] items-center gap-2 border-t border-edge px-2 py-2 cc2-md:grid-cols-[48px_minmax(0,1.3fr)_170px_100px_minmax(0,1.2fr)_auto] ${
        lifted
          ? 'relative z-10 rounded-md bg-panel shadow-lg outline outline-1 -outline-offset-1 outline-cyan'
          : q.next === item.id
            ? 'rounded-md bg-cyan/5 outline outline-1 -outline-offset-1 outline-cyan'
            : ''
      } ${!lifted && shift !== null ? 'transition-transform duration-150' : ''}`}
    >
      <div
        data-queue-handle={grip ? 'on' : 'off'}
        title={grip ? t('queue.list_hint') : undefined}
        class={`relative select-none ${grip ? `touch-none ${lifted ? 'cursor-grabbing' : 'cursor-grab'}` : ''}`}
        {...(grip ?? {})}
      >
        <Thumb item={item} row />
        <span class="absolute -left-1.5 -top-1.5 grid h-5 min-w-5 place-items-center rounded-full border border-edge bg-panel px-1 text-xs text-muted">
          {index + 1}
        </span>
      </div>
      {/* A narrow window gives the name the whole first line; the copies and the actions share the second. */}
      <div class="col-span-2 min-w-0 cc2-md:col-span-1">
        <div class="flex flex-wrap items-center gap-2 font-semibold [overflow-wrap:anywhere]">
          <span>{item.path}</span>
          {stateTag(item, q)}
        </div>
        <div class="text-xs text-muted">
          {where(item.storage)}
          {note && <span class="text-amber"> · {note}</span>}
        </div>
      </div>
      <div class="order-last col-span-2 cc2-md:order-none cc2-md:col-span-1">
        <div class="inline-flex h-8 items-center rounded-md border border-edge bg-field">
          <Button
            variant="ghost"
            class="min-h-7 px-2"
            aria-label={t('queue.fewer_copies')}
            disabled={busy || item.copies <= 1 || item.copies <= item.done || (running && item.copies <= item.done + 1)}
            onClick={() => copies(item.copies - 1)}
          >
            <Minus {...I} />
          </Button>
          <span class="min-w-14 text-center" data-queue-copies>
            <strong class="font-semibold">{copiesLeft(item)}</strong>
            <span class="text-muted"> / {item.copies}</span>
          </span>
          <Button
            variant="ghost"
            class="min-h-7 px-2"
            aria-label={t('queue.more_copies')}
            disabled={busy || item.copies >= 99}
            onClick={() => copies(item.copies + 1)}
          >
            <Plus {...I} />
          </Button>
        </div>
        <div class="text-xs text-muted">{item.done ? tpl('queue.done_k', { k: item.done }) : t('queue.not_yet')}</div>
      </div>
      <span class="hidden cc2-md:inline">
        {item.estimated_seconds === null ? '—' : duration(item.estimated_seconds)}
      </span>
      <div class="hidden cc2-md:block">
        <Settings item={item} lib={lib} />
      </div>
      <div class="order-last flex justify-end gap-1 cc2-md:order-none">
        <Button
          variant="ghost"
          class="px-2"
          aria-label={t('queue.move_up')}
          disabled={busy || running || index === 0}
          onClick={() => move(index - 1)}
        >
          <ChevronUp {...I} />
        </Button>
        <Button
          variant="ghost"
          class="px-2"
          aria-label={t('queue.move_down')}
          disabled={busy || running || index >= jobs.length - 1}
          onClick={() => move(index + 1)}
        >
          <ChevronDown {...I} />
        </Button>
        <Button
          variant="ghost"
          class="px-2 text-red"
          aria-label={t('queue.remove')}
          disabled={busy || running}
          onClick={remove}
          data-queue-remove
        >
          <Trash2 {...I} />
        </Button>
      </div>
    </div>
  )
}

// A drag as the list shows it: the job, where it was taken and where it would land, how much room the others
// make for it, and whether it has been dropped and waits for the queue to come back in the new order.
type Drag = { id: number; from: number; to: number; height: number; dropped: boolean }
// A drag as the pointer needs it between two events; none of it is worth a render.
type Held = {
  pointer: number
  handle: HTMLElement
  row: HTMLElement
  y: number
  startY: number
  scroll: number
  tops: number[]
  heights: number[]
  from: number
  to: number
  min: number
  roof: number
  seen: number
  frame: number
  key: (e: KeyboardEvent) => void
}

// The open jobs in their order. A job is dragged by its picture: the others make room while it moves, and the
// drop is one /api/queue/reorder, as with the arrows. The dragged row is moved directly, so the list is
// rendered again only when its place among the others changes. A job that prints at the head stays there.
const Jobs = ({ q, lib }: { q: QueueData; lib: PlateLibrary | null }) => {
  const jobs = openJobs(q)
  const order = jobs.map(job => job.id).join()
  const min = jobs.length && jobs[0].id === q.active ? 1 : 0
  const host = useRef<HTMLDivElement>(null)
  const held = useRef<Held | null>(null)
  const moved = useRef<HTMLElement | null>(null)
  const [drag, setDrag] = useState<Drag | null>(null)

  const settle = () => {
    const row = moved.current
    moved.current = null
    if (row) row.style.transform = row.style.transition = ''
    setDrag(null)
  }
  const release = () => {
    const h = held.current
    held.current = null
    if (!h) return null
    cancelAnimationFrame(h.frame)
    window.removeEventListener('keydown', h.key)
    if (h.handle.hasPointerCapture(h.pointer)) h.handle.releasePointerCapture(h.pointer)
    return h
  }
  const cancel = () => {
    if (release()) settle()
  }
  // The row follows the pointer between the first place it may take and the last; it would land past every
  // row whose middle its leading edge has crossed. The rows are measured once, when the drag starts.
  const place = () => {
    const h = held.current
    if (!h) return
    const last = h.tops.length - 1
    const dy = Math.max(
      h.tops[h.min] - h.tops[h.from],
      Math.min(
        h.tops[last] + h.heights[last] - h.tops[h.from] - h.heights[h.from],
        h.y - h.startY + window.scrollY - h.scroll
      )
    )
    h.row.style.transform = `translateY(${dy}px)`
    const top = h.tops[h.from] + dy
    let to = h.from
    for (let i = h.from + 1; i <= last && top + h.heights[h.from] > h.tops[i] + h.heights[i] / 2; i++) to = i
    for (let i = h.from - 1; i >= h.min && top < h.tops[i] + h.heights[i] / 2; i--) to = i
    if (to === h.to) return
    h.to = to
    setDrag(d => d && { ...d, to })
  }
  // Near the top bar or the bottom of the window the page scrolls under the pointer. However the page came to
  // scroll (this, a wheel, the keyboard), the row stays under the pointer.
  const tick = () => {
    const h = held.current
    if (!h) return
    const edge = 64
    const past =
      h.y < h.roof + edge ? h.y - h.roof - edge : h.y > window.innerHeight - edge ? h.y - window.innerHeight + edge : 0
    if (past) window.scrollBy({ top: Math.max(-20, Math.min(20, past / 3)), behavior: 'instant' })
    if (window.scrollY !== h.seen) {
      h.seen = window.scrollY
      place()
    }
    h.frame = requestAnimationFrame(tick)
  }
  const start = (e: PointerEvent, index: number) => {
    if (held.current || drag || (e.pointerType === 'mouse' && e.button !== 0)) return
    const rows = [...(host.current?.querySelectorAll<HTMLElement>('[data-queue-item]') ?? [])]
    if (rows.length !== jobs.length || index < min) return
    const handle = e.currentTarget as HTMLElement
    e.preventDefault()
    handle.setPointerCapture(e.pointerId)
    const boxes = rows.map(row => row.getBoundingClientRect())
    const key = (k: KeyboardEvent) => {
      if (k.key === 'Escape') cancel()
    }
    held.current = {
      pointer: e.pointerId,
      handle,
      row: rows[index],
      y: e.clientY,
      startY: e.clientY,
      scroll: window.scrollY,
      tops: boxes.map(box => box.top),
      heights: boxes.map(box => box.height),
      from: index,
      to: index,
      min,
      roof: document.querySelector('.cc2-topbar')?.getBoundingClientRect().bottom ?? 0,
      seen: window.scrollY,
      frame: requestAnimationFrame(tick),
      key,
    }
    moved.current = rows[index]
    window.addEventListener('keydown', key)
    setDrag({ id: jobs[index].id, from: index, to: index, height: boxes[index].height, dropped: false })
  }
  // The row slides into its place and stays there until the queue comes back in the new order (the effect
  // below then lets go of it), or until the change is refused and it goes back.
  const drop = () => {
    const h = release()
    if (!h) return
    if (h.to === h.from) {
      settle()
      return
    }
    let rest = 0
    if (h.to > h.from) for (let i = h.from + 1; i <= h.to; i++) rest += h.heights[i]
    else for (let i = h.to; i < h.from; i++) rest -= h.heights[i]
    h.row.style.transition = 'transform 150ms'
    h.row.style.transform = `translateY(${rest}px)`
    setDrag(d => d && { ...d, dropped: true })
    act(() => reorderQueued(jobs[h.from].id, q.items.indexOf(jobs[h.to]))).finally(() => {
      if (moved.current === h.row) settle()
    })
  }
  const grip = (index: number): Grip => ({
    onPointerDown: e => start(e, index),
    onPointerMove: e => {
      const h = held.current
      if (!h || e.pointerId !== h.pointer) return
      h.y = e.clientY
      place()
    },
    onPointerUp: e => {
      if (held.current?.pointer === e.pointerId) drop()
    },
    onPointerCancel: e => {
      if (held.current?.pointer === e.pointerId) cancel()
    },
    // The browser took the pointer away without an "up" (the window lost it): the job goes back.
    onLostPointerCapture: e => {
      if (held.current?.pointer === e.pointerId) cancel()
    },
  })
  // The list changed (the drop arrived, a copy finished, a job was added or removed): nothing of a drag applies
  // to the new one. Before the browser paints, so a dropped row is never seen jumping.
  useLayoutEffect(() => {
    release()
    settle()
  }, [order])
  useEffect(() => () => void release(), [])
  return (
    <div ref={host} data-queue-rows class={drag ? 'select-none' : undefined}>
      {jobs.map((item, index) => {
        const lifted = drag?.id === item.id
        const aside =
          !drag || lifted
            ? 0
            : index > drag.from && index <= drag.to
              ? -1
              : index >= drag.to && index < drag.from
                ? 1
                : 0
        return (
          <Row
            key={item.id}
            item={item}
            index={index}
            q={q}
            lib={lib}
            grip={index >= min && jobs.length - min > 1 && !drag?.dropped ? grip(index) : null}
            lifted={lifted}
            shift={drag ? aside * drag.height : null}
          />
        )
      })}
    </div>
  )
}

const Empty = () => (
  <Card data-queue-empty>
    <CardHead icon="queue" title="queue.empty_title" />
    <div class="grid place-items-center gap-2 py-6 text-center">
      <Icon n="queue" class="size-12 text-edge" />
      <p class="max-w-md text-muted">{t('queue.empty_hint')}</p>
      <Button variant="primary" onClick={() => openPage('files')}>
        {t('queue.open_files')}
      </Button>
    </div>
  </Card>
)

// The queue is not there yet, or not at all: one card says why.
const Closed = ({ text, tone }: { text: string; tone: 'muted' | 'red' }) => (
  <Card>
    <CardHead icon="queue" title="queue.list_title" />
    <p class={tone === 'red' ? 'text-red' : 'text-muted'}>{text}</p>
  </Card>
)

export const Queue = () => {
  usePoll(refreshQueue, 3000)
  usePoll(refreshPrinter, 1500)
  usePoll(refreshCanvas, 10000)
  usePoll(refreshPlates, 15000)
  const { data: q, ok } = queue.use()
  const lib = plates.use().data
  const [mesh, setMesh] = useState<MeshSides | null>(null)
  const [filaments, setFilaments] = useState<Filament[] | null>(null)
  const [showDone, setShowDone] = useState(false)
  const next = queueNext(q)
  const active = queueActive(q)
  const headKey = next ? fileKey(next) : ''
  // One read-only mesh query when the page opens: it goes to the printer, so it is never polled.
  useEffect(() => {
    let dead = false
    readMeshReadiness().then(r => !dead && setMesh(r.availability))
    return () => {
      dead = true
    }
  }, [])
  // The head job's filaments, for the slot check; the backend caches the scan per file identity.
  useEffect(() => {
    setFilaments(null)
    if (!next?.present) return
    let dead = false
    const cached = inspected.get(headKey)
    if (cached) {
      setFilaments(cached)
      return
    }
    ;(async () => {
      try {
        const r = await request('/api/gcode-files/inspect', {
          method: 'POST',
          headers: { 'Content-Type': 'text/plain;charset=UTF-8' },
          body: headKey,
        })
        const list: Filament[] = Array.isArray(r?.filaments) ? r.filaments : []
        inspected.set(headKey, list)
        if (!dead) setFilaments(list)
      } catch {
        /* the check shows the slot without the file's material */
      }
    })()
    return () => {
      dead = true
    }
  }, [headKey])
  const totals = queueTotals(q)
  const tags = q?.available ? (
    <>
      <Tag>{tpl('queue.jobs_n', { n: totals.jobs })}</Tag>
      <Tag>{tpl('queue.copies_ahead_n', { n: totals.copies })}</Tag>
      {totals.copies > 0 && (
        <Tag>{tpl('queue.time_ahead', { time: `${totals.unknown ? '> ' : ''}${duration(totals.seconds)}` })}</Tag>
      )}
      <Button variant="primary" onClick={() => openPage('files')} data-queue-add>
        <Plus {...I} />
        {t('queue.add_file')}
      </Button>
    </>
  ) : undefined
  const done = finishedJobs(q)
  return (
    <Page title="queue.title" sub="queue.subtitle" tags={tags}>
      {!q ? (
        <Closed text={t(ok ? 'history.loading' : 'app.printer_unreachable')} tone="muted" />
      ) : !q.available ? (
        <Closed text={tpl('queue.unavailable', { error: q.error || t('state.unknown') })} tone="red" />
      ) : !q.items.length ? (
        <Empty />
      ) : (
        <>
          <div class="grid gap-3.5 cc2-lg:grid-cols-[minmax(0,1.3fr)_minmax(0,0.7fr)]">
            {active ? (
              <Hero q={q} item={active} active filaments={filaments} mesh={mesh} lib={lib} />
            ) : next ? (
              <Hero q={q} item={next} active={false} filaments={filaments} mesh={mesh} lib={lib} />
            ) : (
              <Card data-queue-hero={openJobs(q).length ? 'none' : 'done'}>
                <CardHead icon="queue" title="queue.next_job" />
                <p class="text-muted">{t(openJobs(q).length ? 'queue.nothing_to_start' : 'queue.all_done')}</p>
              </Card>
            )}
            <div class="grid content-start gap-3.5">
              <Totals q={q} />
              <Policy policy={q.policy} />
            </div>
          </div>
          <Card class="mt-3.5" data-queue-list>
            <CardHead icon="queue" title="queue.list_title" end={t('queue.list_hint')} />
            <div class="hidden cc2-md:grid cc2-md:grid-cols-[48px_minmax(0,1.3fr)_170px_100px_minmax(0,1.2fr)_auto] gap-2 px-2 pb-1.5 text-xs text-muted">
              <span>#</span>
              <span>{t('queue.col_file')}</span>
              <span>{t('queue.col_copies')}</span>
              <span>{t('queue.col_per_copy')}</span>
              <span>{t('queue.col_settings')}</span>
              <span class="text-right">{t('queue.col_actions')}</span>
            </div>
            <Jobs q={q} lib={lib} />
            {!openJobs(q).length && <p class="p-3 text-muted">{t('queue.all_done')}</p>}
            {done.length > 0 && (
              <div class="mt-2.5 border-t border-edge pt-2.5">
                <div class="flex items-center justify-between gap-2">
                  <Button variant="ghost" onClick={() => setShowDone(!showDone)} aria-expanded={showDone}>
                    {showDone ? <ChevronDown {...I} /> : <ChevronRight {...I} />}
                    {tpl('queue.finished_n', { n: done.length })}
                  </Button>
                  <Button variant="ghost" class="text-muted" onClick={() => act(() => clearFinished())}>
                    {t('queue.clear_finished')}
                  </Button>
                </div>
                {showDone &&
                  done.map(item => (
                    <div
                      key={item.id}
                      class="flex flex-wrap items-center gap-2 px-2 py-1.5 text-muted"
                      data-queue-done={item.id}
                    >
                      <Tag tone="ok">
                        {item.done} / {item.copies}
                      </Tag>
                      <span class="[overflow-wrap:anywhere]">{item.path}</span>
                      <span class="ml-auto text-xs">{new Date(item.added * 1000).toLocaleString()}</span>
                    </div>
                  ))}
              </div>
            )}
          </Card>
        </>
      )}
    </Page>
  )
}
