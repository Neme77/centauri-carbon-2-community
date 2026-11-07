import { useEffect, useState } from 'preact/hooks'
import { ChevronDown, ChevronRight, ChevronUp, FileText, Minus, Play, Plus, SkipForward, Trash2 } from 'lucide-preact'
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
import {
  clearFinished,
  copiesLeft,
  finishedJobs,
  holdQueue,
  mappingSlots,
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
  startQueued,
} from '@/lib/queue'

const I = { size: 16, strokeWidth: 1 }
type Tone = 'ok' | 'warn' | 'bad' | 'info'
type Check = { tone: Tone; text: string }
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
  const slots = mappingSlots(item.mapping)
  if (!slots.length) checks.push({ tone: 'info', text: t('queue.check_external') })
  for (const { tool, slot } of slots) {
    const tray = trays.find(x => x && Number(x.tray_id) === slot)
    const label = (tray && (tray.filament_name || tray.filament_type)) || ''
    const type = String(tray?.filament_type || tray?.filament_name || '')
    const wanted = filaments?.find(f => f.tool === tool)?.material || ''
    const n = slot + 1
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
        <span class={c.tone === 'warn' ? 'text-amber' : c.tone === 'bad' ? 'text-red' : ''}>{c.text}</span>
      </li>
    ))}
  </ul>
)

// The job's print settings, as the dialog saved them.
const Settings = ({ item, lib }: { item: QueueItem; lib: PlateLibrary | null }) => {
  const slots = mappingSlots(item.mapping)
  const chips: string[] = [tpl('queue.side_n', { side: item.side })]
  chips.push(
    slots.length ? slots.map(s => tpl('common.slot_n', { n: s.slot + 1 })).join(' · ') : t('queue.external_feed')
  )
  if (item.leveling !== 'saved') chips.push(t('queue.calibration'))
  if (item.timelapse) chips.push(t('queue.timelapse'))
  if (item.nozzle) chips.push(tpl('queue.nozzle_x', { name: nozzleName(lib, item.nozzle) || item.nozzle }))
  return (
    <div class="flex flex-wrap gap-1.5">
      {chips.map(c => (
        <Tag key={c}>{c}</Tag>
      ))}
    </div>
  )
}

const Thumb = ({ item }: { item: QueueItem | null }) => {
  const [url, setUrl] = useState('')
  const k = item ? fileKey(item) : ''
  useEffect(() => {
    setUrl('')
    if (!item) return
    let dead = false,
      made = ''
    ;(async () => {
      try {
        const r = await fetch('/api/gcode-files/thumbnail', {
          method: 'POST',
          headers: { 'Content-Type': 'text/plain;charset=UTF-8', 'X-CC2-Request': '1' },
          body: `${item.storage}\n${item.path}`,
        })
        const blob = await r.blob()
        if (!r.ok || !blob.type.startsWith('image/')) throw Error('no thumbnail')
        made = URL.createObjectURL(blob)
        if (!dead) setUrl(made)
      } catch {
        /* the placeholder stays */
      }
    })()
    return () => {
      dead = true
      if (made) URL.revokeObjectURL(made)
    }
  }, [k])
  return (
    <div class="grid size-28 shrink-0 place-items-center overflow-hidden rounded-lg border border-edge bg-well">
      {url ? (
        <img src={url} alt={t('files.g_code_model_preview')} class="size-full object-contain p-1.5" />
      ) : (
        <FileText size={40} strokeWidth={1} class="text-edge" />
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
  const canStart = !active && item.present && v.idle && ok && Boolean(d?.connected) && !q.active
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
          <div class="text-lg font-semibold [overflow-wrap:anywhere]">{item.path}</div>
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
      <div class="mt-3.5 flex flex-wrap items-center gap-2.5 border-t border-edge pt-3">
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
          <Button class="min-h-11" onClick={() => openPage('job')}>
            {t('queue.open_job')}
          </Button>
        ) : (
          <Button class="min-h-11" onClick={skip} disabled={busy || openJobs(q).length < 2} data-queue-skip>
            <SkipForward {...I} />
            {t('queue.skip')}
          </Button>
        )}
        {!active && q.auto_in !== null && (
          <span class="flex items-center gap-2 text-xs text-amber" data-queue-auto>
            {tpl('queue.auto_in_n', { n: q.auto_in })}
            <Button class="min-h-8 px-2 text-xs" onClick={() => act(() => holdQueue())}>
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
        <div class="flex items-center justify-between gap-2.5 border-t border-edge pt-2.5">
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
        : ''

const Row = ({ item, index, q, lib }: { item: QueueItem; index: number; q: QueueData; lib: PlateLibrary | null }) => {
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
      class={`grid grid-cols-[28px_minmax(0,1fr)_auto] items-center gap-2.5 border-t border-edge px-2 py-2 cc2-md:grid-cols-[28px_minmax(0,1.3fr)_170px_100px_minmax(0,1.2fr)_auto] ${
        q.next === item.id ? 'rounded-md bg-cyan/5 outline outline-1 -outline-offset-1 outline-cyan' : ''
      }`}
    >
      <span class="text-muted">{index + 1}</span>
      <div class="min-w-0">
        <div class="flex flex-wrap items-center gap-2 font-semibold [overflow-wrap:anywhere]">
          <span>{item.path}</span>
          {stateTag(item, q)}
        </div>
        <div class="text-xs text-muted">
          {where(item.storage)}
          {note && <span class="text-amber"> · {note}</span>}
        </div>
      </div>
      <div class="cc2-md:order-none order-last col-span-3 cc2-md:col-span-1">
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
      <div class="flex justify-end gap-1">
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

const Empty = () => (
  <Card data-queue-empty>
    <CardHead icon="queue" title="queue.empty_title" />
    <div class="grid place-items-center gap-2.5 py-6 text-center">
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
            <div class="hidden cc2-md:grid cc2-md:grid-cols-[28px_minmax(0,1.3fr)_170px_100px_minmax(0,1.2fr)_auto] gap-2.5 px-2 pb-1.5 text-xs text-muted">
              <span>#</span>
              <span>{t('queue.col_file')}</span>
              <span>{t('queue.col_copies')}</span>
              <span>{t('queue.col_per_copy')}</span>
              <span>{t('queue.col_settings')}</span>
              <span class="text-right">{t('queue.col_actions')}</span>
            </div>
            {openJobs(q).map((item, index) => (
              <Row key={item.id} item={item} index={index} q={q} lib={lib} />
            ))}
            {!openJobs(q).length && <p class="p-3 text-muted">{t('queue.all_done')}</p>}
            {done.length > 0 && (
              <div class="mt-2.5 border-t border-edge pt-2.5">
                <div class="flex items-center justify-between gap-2.5">
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
                      class="flex flex-wrap items-center gap-2.5 px-2 py-1.5 text-muted"
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
