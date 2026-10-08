import { useMemo, useRef, useState } from 'preact/hooks'
import { ChevronDown, History as HistoryIcon, Pencil, Scale } from 'lucide-preact'
import { cn } from '@/lib/utils'
import { ls } from '@/lib/store'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Tag } from '@/components/ui/badge'
import { Input } from '@/components/ui/field'
import { Dialog } from '@/components/ui/dialog'
import { Notice, Warn } from '@/components/shared'
import { draftFields, draftFrom, draftProblem, placeName, SpoolEditor, SpoolMeter, Swatch } from '@/components/spool-ui'
import { errText, notify } from '@/lib/api'
import { ask } from '@/lib/confirm'
import { type Key, t, tpl } from '@/lib/i18n'
import { usePoll } from '@/lib/poll'
import { printer, view } from '@/lib/state'
import {
  adjustSpool,
  assignSpool,
  colourWord,
  deleteSpool,
  EXTERNAL,
  findSpool,
  fits,
  g,
  gramsFor,
  isLow,
  isSealed,
  matches,
  openChooser,
  percent,
  refreshSpools,
  type Spool,
  type SpoolEvent,
  type SpoolLibrary,
  type SpoolSlot,
  type SpoolStack,
  saveSpool,
  setTracking,
  slotOf,
  spoolHierarchy,
  spoolLabel,
  spools,
} from '@/lib/spools'

const day = (s: number) => (s > 0 ? new Date(s * 1000).toLocaleDateString() : '—')
const moment = (s: number) =>
  new Date(s * 1000).toLocaleString([], { day: '2-digit', month: '2-digit', hour: '2-digit', minute: '2-digit' })

// One action at a time on this page: every button runs through `act`.
type Act = (fn: () => Promise<unknown>) => Promise<void>
const useAct = () => {
  const lock = useRef(false)
  const [busy, setBusy] = useState(false)
  const act = async (fn: () => Promise<unknown>) => {
    if (lock.current) return
    lock.current = true
    setBusy(true)
    try {
      await fn()
    } catch (e) {
      notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
    } finally {
      lock.current = false
      setBusy(false)
      void refreshSpools()
    }
  }
  return { busy, act }
}

const KINDS: Record<SpoolEvent['kind'], Key> = {
  print: 'spools.kind_print',
  runout: 'spools.kind_runout',
  correction: 'spools.kind_correction',
  new: 'spools.kind_new',
}
const RESULTS: Record<string, Key> = {
  complete: 'spools.result_complete',
  cancelled: 'spools.result_cancelled',
  canceled: 'spools.result_cancelled',
  error: 'spools.result_error',
  changed: 'spools.result_changed',
  moved: 'spools.result_changed',
  removed: 'spools.result_removed',
  runout: 'spools.result_runout',
  late: 'spools.result_late',
  archived: 'spools.result_changed',
  deleted: 'spools.result_changed',
  ended: 'spools.result_ended',
  'tracking off': 'spools.result_tracking_off',
  weighed: 'spools.result_weighed',
  set: 'spools.result_set',
}

const Tracking = ({ lib, busy, act }: { lib: SpoolLibrary; busy: boolean; act: Act }) => (
  <Card>
    <CardHead
      icon="spool"
      title="spools.tracking"
      end={
        <Tag tone={lib.enabled ? 'ok' : 'default'}>{t(lib.enabled ? 'spools.tracking_on' : 'spools.tracking_off')}</Tag>
      }
    />
    <p class="text-[13px] text-muted">{t('spools.how_it_works')}</p>
    <div class="mt-3 flex flex-wrap items-center gap-3">
      <Button
        variant={lib.enabled ? 'default' : 'primary'}
        disabled={busy}
        aria-pressed={lib.enabled}
        onClick={() =>
          act(async () => {
            await setTracking(!lib.enabled)
            notify(t(lib.enabled ? 'spools.off_toast' : 'spools.on_toast'))
          })
        }
      >
        {t(lib.enabled ? 'spools.turn_off' : 'spools.turn_on')}
      </Button>
    </div>
    {!lib.enabled && <Notice>{t('spools.off_note')}</Notice>}
    {lib.enabled && !lib.canvas && <Notice>{t('spools.no_canvas')}</Notice>}
  </Card>
)

const SlotCard = ({ lib, s, busy, act }: { lib: SpoolLibrary; s: SpoolSlot; busy: boolean; act: Act }) => {
  const spool = findSpool(lib, s.spool)
  const tray = s.printer
  const external = s.slot === EXTERNAL
  const empty = !external && tray?.status === 0
  return (
    <div class="flex min-w-0 flex-col gap-2 rounded-lg border border-edge bg-field/40 p-3" data-slot={s.slot}>
      <div class="flex flex-wrap items-center gap-1.5">
        <strong class="mr-auto text-sm">{placeName(s.slot)}</strong>
        {tray?.status === 2 && <Tag tone="ok">{t('spools.feeding')}</Tag>}
        {s.runout && <Tag tone="warning">{t('spools.ran_out')}</Tag>}
        {s.question && <Tag tone="warning">{t('spools.question_tag')}</Tag>}
      </div>
      {!external && (
        <div class="flex min-h-5 items-center gap-2 text-xs text-muted">
          {!tray ? (
            t('spools.tray_unknown')
          ) : empty ? (
            t('spools.tray_empty')
          ) : (
            <>
              <Swatch color={tray.color} />
              <span class="[overflow-wrap:anywhere]">
                {[tray.brand, tray.name || tray.type].filter(Boolean).join(' ')}
              </span>
            </>
          )}
        </div>
      )}
      {spool ? (
        <div class="grid gap-1.5">
          <div class="flex items-center gap-2">
            <Swatch color={spool.color} class="size-5" />
            <b class="min-w-0 text-[13px] [overflow-wrap:anywhere]">{spoolLabel(spool)}</b>
          </div>
          <SpoolMeter spool={spool} />
          {tray && !empty && !fits(spool, tray) && <span class="text-xs text-amber">{t('spools.differs')}</span>}
        </div>
      ) : (
        <span class="text-xs text-muted">{t(s.question ? 'spools.question_hint' : 'spools.no_spool')}</span>
      )}
      {external && <span class="text-xs text-muted">{t('spools.external_hint')}</span>}
      <div class="mt-auto flex flex-wrap gap-2 pt-1">
        <Button
          class="text-xs"
          variant={s.question ? 'primary' : 'default'}
          disabled={busy || empty}
          onClick={() => openChooser(s.slot, s.question ? 'question' : 'manual')}
        >
          {t('spools.choose')}
        </Button>
        {spool && (
          <Button
            class="text-xs"
            variant="ghost"
            disabled={busy}
            onClick={() =>
              act(async () => {
                await assignSpool(s.slot, '')
                notify(tpl('spools.emptied_toast', { place: placeName(s.slot) }))
              })
            }
          >
            {t('spools.take_out')}
          </Button>
        )}
      </div>
    </div>
  )
}

const InPrinter = ({ lib, busy, act }: { lib: SpoolLibrary; busy: boolean; act: Act }) => {
  const used = lib.job.slots.map((x, slot) => ({ ...x, slot })).filter(x => Math.abs(x.mm) >= 1)
  return (
    <Card>
      <CardHead icon="canvas" title="spools.in_printer" />
      <div class="grid gap-2.5 cc2-sm:grid-cols-2 cc2-lg:grid-cols-3 cc2-xl:grid-cols-5">
        {lib.slots.map(s => (
          <SlotCard key={s.slot} lib={lib} s={s} busy={busy} act={act} />
        ))}
      </div>
      {lib.job.active && (
        <div class="mt-3 rounded-md border border-edge bg-field/60 px-3 py-2.5 text-xs" data-testid="spool-job">
          <div class="mb-1 text-muted">
            {t('spools.current_print')} · <b class="text-fg [overflow-wrap:anywhere]">{lib.job.file}</b>
          </div>
          {used.length ? (
            used.map(x => {
              const spool = findSpool(lib, x.spool)
              return (
                <div key={x.slot} class="flex flex-wrap gap-x-2">
                  <span>{placeName(x.slot)}:</span>
                  <b>{g(spool ? x.grams : gramsFor(x.mm, 1.75, 1.24))} g</b>
                  <span class="text-muted">{spool ? spoolLabel(spool) : t('spools.untracked')}</span>
                </div>
              )
            })
          ) : (
            <span class="text-muted">{t('spools.nothing_yet')}</span>
          )}
        </div>
      )}
    </Card>
  )
}

const History = ({ lib, events, spool }: { lib: SpoolLibrary; events: SpoolEvent[]; spool?: boolean }) =>
  events.length ? (
    // A list rather than a table: on a phone the file names wrap under the entry instead of squeezing a column.
    <ul class="grid text-xs">
      {events.map((e, i) => {
        const s = findSpool(lib, e.spool)
        return (
          <li
            key={`${e.time}-${i}`}
            class="grid grid-cols-[minmax(0,1fr)_auto] gap-x-3 gap-y-0.5 border-b border-edge py-1.5"
            data-event={e.kind}
          >
            <span class="flex min-w-0 flex-wrap items-center gap-x-2.5 gap-y-0.5">
              <span class="whitespace-nowrap text-muted">{moment(e.time)}</span>
              {spool && (
                <span class="flex min-w-0 items-center gap-1.5">
                  {s && <Swatch color={s.color} class="size-3" />}
                  <span class="[overflow-wrap:anywhere]">
                    {s ? spoolLabel(s) : t(e.spool ? 'spools.deleted_spool' : 'spools.untracked')}
                  </span>
                </span>
              )}
              {e.slot >= 0 && <span class="whitespace-nowrap text-muted">{placeName(e.slot)}</span>}
              <span class="whitespace-nowrap">{t(KINDS[e.kind])}</span>
            </span>
            <b class={cn('text-right whitespace-nowrap tabular-nums', e.grams > 0 && 'text-green')}>
              {e.grams > 0 ? '+' : e.grams < 0 ? '−' : ''}
              {g(Math.abs(e.grams))} g
            </b>
            {(e.job || e.result) && (
              <span class="col-span-2 text-muted [overflow-wrap:anywhere]">
                {[e.job, e.result && (RESULTS[e.result] ? t(RESULTS[e.result]) : e.result)].filter(Boolean).join(' · ')}
              </span>
            )}
          </li>
        )
      })}
    </ul>
  ) : (
    <Notice>{t('spools.no_history')}</Notice>
  )

const kg = (grams: number) => (grams / 1000).toFixed(1)
// Compact buttons for the inventory rows; `!` wins over the Button's own size classes.
const small = 'min-h-7! px-2! py-0.5! text-xs'
const icon = 'min-h-7! px-1.5! py-0.5!'
const I = { size: 16, strokeWidth: 1 }

// One spool of a colour, shown when its stack is opened. The stack above already names the brand, kind and
// colour, so a row keeps to what tells identical spools apart: sealed or how much is left, the slot, the
// last use and the note.
const SpoolRow = ({
  lib,
  s,
  busy,
  edit,
  weigh,
}: {
  lib: SpoolLibrary
  s: Spool
  busy: boolean
  edit: () => void
  weigh: () => void
}) => {
  const [open, setOpen] = useState(false)
  const where = slotOf(lib, s.id)
  const low = !s.archived && isLow(s)
  const sealed = isSealed(s)
  return (
    <div class="grid gap-1.5 rounded-md border border-edge bg-panel px-2.5 py-2" data-spool={s.id}>
      <div class="flex flex-wrap items-center gap-x-2 gap-y-1.5 text-xs">
        {sealed && <Tag>{t('spools.sealed')}</Tag>}
        {where >= 0 && <Tag tone="ok">{placeName(where)}</Tag>}
        {low && <Tag tone="warning">{t(s.remaining <= 0 ? 'spools.empty' : 'spools.low_tag')}</Tag>}
        <span class="whitespace-nowrap tabular-nums">
          <b class={cn('text-[13px]', low && 'text-amber')}>{g(s.remaining)} g</b>
          {!sealed && <span class="text-muted"> {tpl('spools.of_net', { net: g(s.net) })}</span>}
        </span>
        <span class="ml-auto flex gap-1">
          <Button class={icon} title={t('spools.weigh')} aria-label={t('spools.weigh')} disabled={busy} onClick={weigh}>
            <Scale {...I} />
          </Button>
          <Button class={icon} title={t('spools.edit')} aria-label={t('spools.edit')} disabled={busy} onClick={edit}>
            <Pencil {...I} />
          </Button>
          <Button
            class={icon}
            title={t('common.history')}
            aria-label={t('common.history')}
            aria-expanded={open}
            onClick={() => setOpen(!open)}
          >
            <HistoryIcon {...I} />
          </Button>
        </span>
      </div>
      <div class="h-1 overflow-hidden rounded-full bg-edge">
        <i class={cn('block h-full rounded-full', low ? 'bg-amber' : 'bg-cyan')} style={{ width: `${percent(s)}%` }} />
      </div>
      {(s.used > 0 || s.note) && (
        <div class="flex min-w-0 gap-x-3 text-xs text-muted">
          {s.used > 0 && <span class="whitespace-nowrap">{tpl('spools.last_used', { date: day(s.used) })}</span>}
          {s.note && (
            <span class="min-w-0 truncate" title={s.note}>
              {s.note}
            </span>
          )}
        </div>
      )}
      {open && <History lib={lib} events={lib.log.filter(e => e.spool === s.id)} />}
    </div>
  )
}

// Identical spools of one colour as a single row: how many, how full each one is, where they are.
const StackRow = ({
  lib,
  stack,
  busy,
  expanded,
  edit,
  weigh,
  copy,
}: {
  lib: SpoolLibrary
  stack: SpoolStack
  busy: boolean
  expanded: boolean
  edit: (s: Spool) => void
  weigh: (s: Spool) => void
  copy: (s: Spool) => void
}) => {
  const [open, setOpen] = useState(false)
  const shown = open || expanded
  const places = stack.spools.map(s => slotOf(lib, s.id)).filter(slot => slot >= 0)
  const bars = stack.spools.slice(0, 10)
  return (
    <div class="rounded-md border border-edge bg-field/40" data-stack={stack.key}>
      <button
        type="button"
        class="flex w-full flex-wrap items-center gap-x-2.5 gap-y-1 px-2.5 py-2 text-left hover:bg-field"
        aria-expanded={shown}
        onClick={() => setOpen(!open)}
      >
        <Swatch color={stack.color} class="size-5" />
        <b class="min-w-0 text-[13px] [overflow-wrap:anywhere]">{stack.label || t(colourWord(stack.color))}</b>
        {stack.spools.length > 1 && <span class="text-xs text-muted">×{stack.spools.length}</span>}
        <span class="flex h-4 items-end gap-0.5" aria-hidden="true">
          {bars.map(s => (
            <i key={s.id} class="relative block h-4 w-1.5 overflow-hidden rounded-sm bg-edge">
              <i
                class={cn('absolute inset-x-0 bottom-0', isLow(s) ? 'bg-amber' : 'bg-cyan')}
                style={{ height: `${percent(s)}%` }}
              />
            </i>
          ))}
          {stack.spools.length > bars.length && (
            <span class="text-[11px] text-muted">+{stack.spools.length - bars.length}</span>
          )}
        </span>
        <span class="ml-auto flex flex-wrap items-center justify-end gap-1.5">
          {places.map(slot => (
            <Tag key={slot} tone="ok">
              {placeName(slot)}
            </Tag>
          ))}
          {stack.low && <Tag tone="warning">{t('spools.low_tag')}</Tag>}
          <b class="text-[13px] tabular-nums">{g(stack.grams)} g</b>
          <ChevronDown
            size={16}
            strokeWidth={1.5}
            class={cn('shrink-0 transition-transform', !shown && '-rotate-90')}
          />
        </span>
      </button>
      {shown && (
        <div class="grid gap-1.5 border-t border-edge p-2">
          {stack.spools.map(s => (
            <SpoolRow key={s.id} lib={lib} s={s} busy={busy} edit={() => edit(s)} weigh={() => weigh(s)} />
          ))}
          {!stack.spools[0].archived && (
            <Button
              variant="ghost"
              class={cn(small, 'justify-self-start')}
              disabled={busy}
              onClick={() => copy(stack.spools[0])}
            >
              {t('spools.add_same')}
            </Button>
          )}
        </div>
      )}
    </div>
  )
}

const SpoolForm = ({
  spool,
  template,
  busy,
  act,
  close,
}: {
  spool: Spool | null
  template: Spool | null
  busy: boolean
  act: Act
  close: () => void
}) => {
  // "Add another like this": the same spool, full.
  const [draft, setDraft] = useState(() =>
    spool
      ? draftFrom(spool)
      : template
        ? { ...draftFrom(template), remaining: draftFrom(template).net }
        : draftFrom(null)
  )
  const save = () =>
    act(async () => {
      const problem = draftProblem(draft, !spool)
      if (problem) return notify(t(problem), 'error')
      await saveSpool(spool ? { id: spool.id, ...draftFields(draft, false) } : draftFields(draft, true))
      notify(t('spools.saved_toast'))
      close()
    })
  return (
    <Dialog onClose={close} locked={busy} width={680}>
      <h2 class="mb-4 text-xl font-semibold">{t(spool ? 'spools.edit_spool' : 'spools.new_spool')}</h2>
      <SpoolEditor draft={draft} set={setDraft} create={!spool} />
      {!spool && <p class="mt-3 text-xs text-muted">{t('spools.new_spool_storage')}</p>}
      <div class="mt-5 flex flex-wrap justify-end gap-2.5">
        {spool && (
          <span class="mr-auto flex flex-wrap gap-2">
            <Button
              variant="ghost"
              disabled={busy}
              onClick={() =>
                act(async () => {
                  await saveSpool({ id: spool.id, archived: !spool.archived })
                  notify(t(spool.archived ? 'spools.restored_toast' : 'spools.archived_toast'))
                  close()
                })
              }
            >
              {t(spool.archived ? 'spools.restore' : 'spools.archive')}
            </Button>
            <Button
              variant="danger"
              disabled={busy}
              onClick={() =>
                act(async () => {
                  if (!(await ask(tpl('spools.delete_confirm', { name: spoolLabel(spool) }), true))) return
                  await deleteSpool(spool.id)
                  notify(t('spools.deleted_toast'))
                  close()
                })
              }
            >
              {t('common.delete')}
            </Button>
          </span>
        )}
        <Button onClick={close} disabled={busy}>
          {t('common.cancel')}
        </Button>
        <Button variant="primary" onClick={save} disabled={busy}>
          {t('spools.save')}
        </Button>
      </div>
    </Dialog>
  )
}

// Correct the count: weigh the spool with its empty spool weight, or type the filament left.
const Weigh = ({ spool, busy, act, close }: { spool: Spool; busy: boolean; act: Act; close: () => void }) => {
  const [mode, setMode] = useState<'gross' | 'net'>('gross')
  const [weight, setWeight] = useState('')
  const [tare, setTare] = useState(spool.tare ? String(spool.tare) : '')
  const n = Number(weight),
    empty = Number(tare)
  const number = (v: string) => /^\d+(\.\d+)?$/.test(v.trim())
  const ok =
    number(weight) &&
    (mode === 'net' ? n <= 20000 : number(tare) && empty > 0 && empty <= 5000 && n >= empty && n <= 25000)
  const left = mode === 'gross' ? n - empty : n
  const save = () =>
    act(async () => {
      if (mode === 'gross' && empty !== spool.tare) await saveSpool({ id: spool.id, tare: empty })
      await adjustSpool(spool.id, mode === 'gross' ? { gross: n } : { remaining: n })
      notify(tpl('spools.weighed_toast', { grams: g(left) }))
      close()
    })
  return (
    <Dialog onClose={close} locked={busy} width={520}>
      <h2 class="mb-1 text-xl font-semibold">{t('spools.weigh_title')}</h2>
      <p class="mb-4 text-xs text-muted [overflow-wrap:anywhere]">
        {spoolLabel(spool)} · {tpl('spools.counted_now', { grams: g(spool.remaining) })}
      </p>
      <fieldset class="m-0 mb-3 flex flex-wrap gap-4 border-0 p-0 text-[13px]">
        <legend class="sr-only">{t('spools.weigh_title')}</legend>
        {(['gross', 'net'] as const).map(m => (
          <label key={m} class="flex items-center gap-2">
            <input type="radio" name="cc2-weigh" class="accent-cyan" checked={mode === m} onChange={() => setMode(m)} />
            {t(m === 'gross' ? 'spools.weigh_gross_mode' : 'spools.weigh_net_mode')}
          </label>
        ))}
      </fieldset>
      <div class="grid gap-3 cc2-sm:grid-cols-2">
        <label class="grid gap-1 text-xs">
          <span class="text-muted">{t(mode === 'gross' ? 'spools.weigh_gross' : 'spools.weigh_net')}</span>
          <Input
            type="number"
            inputMode="decimal"
            min="0"
            step="1"
            autofocus
            value={weight}
            onInput={e => setWeight(e.currentTarget.value)}
          />
        </label>
        {mode === 'gross' && (
          <label class="grid gap-1 text-xs">
            <span class="text-muted">{t('spools.tare')}</span>
            <Input
              type="number"
              inputMode="decimal"
              min="1"
              max="5000"
              step="1"
              value={tare}
              onInput={e => setTare(e.currentTarget.value)}
            />
          </label>
        )}
      </div>
      <p class="mt-3 min-h-5 text-[13px]" role="status">
        {ok
          ? tpl('spools.weigh_result', { grams: g(left) })
          : mode === 'gross' && !(empty > 0)
            ? t('spools.weigh_needs_tare')
            : ''}
      </p>
      <div class="mt-5 flex justify-end gap-2.5">
        <Button onClick={close} disabled={busy}>
          {t('common.cancel')}
        </Button>
        <Button variant="primary" onClick={save} disabled={busy || !ok}>
          {t('spools.save')}
        </Button>
      </div>
    </Dialog>
  )
}

// Folded manufacturer sections are remembered per browser.
const FOLDED = 'cc2-spools-folded'

// Manufacturer > kind > colour > spool. Manufacturers with fewer than three spools share "Other".
const Inventory = ({ lib, busy, act }: { lib: SpoolLibrary; busy: boolean; act: Act }) => {
  const [archived, setArchived] = useState(false)
  const [query, setQuery] = useState('')
  const [editing, setEditing] = useState<{ spool: Spool | null; template: Spool | null } | null>(null)
  const [weighing, setWeighing] = useState<Spool | null>(null)
  const [folded, setFolded] = useState<string[]>(() => {
    try {
      const list = JSON.parse(ls.get(FOLDED) || '[]')
      return Array.isArray(list) ? list.map(String) : []
    } catch {
      return []
    }
  })
  const fold = (key: string) => {
    const next = folded.includes(key) ? folded.filter(k => k !== key) : [...folded, key]
    setFolded(next)
    ls.set(FOLDED, JSON.stringify(next))
  }
  const pool = lib.spools.filter(s => s.archived === archived)
  const list = pool.filter(s => matches(s, query))
  const brands = useMemo(() => spoolHierarchy(list), [lib, archived, query])
  const stacks = brands.flatMap(b => b.kinds.flatMap(k => k.stacks))
  const searching = Boolean(query.trim())
  const count = (a: boolean) => lib.spools.filter(s => s.archived === a).length
  const handlers = {
    edit: (s: Spool) => setEditing({ spool: s, template: null }),
    weigh: (s: Spool) => setWeighing(s),
    copy: (s: Spool) => setEditing({ spool: null, template: s }),
  }
  return (
    <Card>
      <CardHead
        icon="layers"
        title="spools.inventory"
        end={
          <Button
            variant="primary"
            class="text-xs"
            disabled={busy}
            onClick={() => setEditing({ spool: null, template: null })}
          >
            {t('spools.add')}
          </Button>
        }
      />
      <div class="mb-2 flex flex-wrap items-center gap-1.5">
        {[false, true].map(a => (
          <Button
            key={String(a)}
            class="text-xs"
            variant={archived === a ? 'active' : 'default'}
            aria-pressed={archived === a}
            onClick={() => setArchived(a)}
          >
            {tpl(a ? 'spools.archived_n' : 'spools.active_n', { n: count(a) })}
          </Button>
        ))}
        <Input
          type="search"
          class="w-full cc2-sm:ml-auto cc2-sm:w-64"
          placeholder={t('spools.search')}
          aria-label={t('spools.search')}
          value={query}
          onInput={e => setQuery(e.currentTarget.value)}
        />
      </div>
      {pool.length > 0 && (
        <p class="mb-3 text-xs text-muted">
          {tpl('spools.summary', {
            n: pool.length,
            kg: kg(pool.reduce((sum, s) => sum + Math.max(0, s.remaining), 0)),
            low: archived
              ? 0
              : spoolHierarchy(pool)
                  .flatMap(b => b.kinds.flatMap(k => k.stacks))
                  .filter(s => s.low).length,
          })}
        </p>
      )}
      <div class="grid gap-2.5">
        {brands.map(brand => {
          const key = brand.other ? '~other' : brand.key
          const header = brand.label || brand.other
          const open = searching || !header || !folded.includes(key)
          return (
            <section key={key} class="rounded-lg border border-edge" data-brand={brand.other ? 'other' : brand.key}>
              {header && (
                <button
                  type="button"
                  class="flex w-full flex-wrap items-baseline gap-x-2.5 px-3 py-2.5 text-left hover:bg-field"
                  aria-expanded={open}
                  onClick={() => fold(key)}
                >
                  <ChevronDown
                    size={16}
                    strokeWidth={1.5}
                    class={cn('shrink-0 self-center transition-transform', !open && '-rotate-90')}
                  />
                  <b class="text-sm">{brand.other ? t('spools.other_brands') : brand.label}</b>
                  <span class="text-xs text-muted">
                    {tpl('spools.count_weight', { n: brand.count, kg: kg(brand.grams) })}
                  </span>
                </button>
              )}
              {open && (
                <div class={cn('grid gap-3.5 p-3', header && 'border-t border-edge')}>
                  {brand.kinds.map(kind => (
                    <div key={kind.key} class="grid gap-1.5" data-kind={kind.key}>
                      <div class="flex flex-wrap items-baseline gap-x-2 text-xs">
                        <b class="text-[13px]">{kind.label}</b>
                        <span class="text-muted">
                          {tpl('spools.count_weight', { n: kind.count, kg: kg(kind.grams) })}
                        </span>
                      </div>
                      <div class="grid items-start gap-1.5 cc2-lg:grid-cols-2 cc2-xl:grid-cols-3">
                        {kind.stacks.map(stack => (
                          <StackRow
                            key={stack.key}
                            lib={lib}
                            stack={stack}
                            busy={busy}
                            expanded={searching}
                            {...handlers}
                          />
                        ))}
                      </div>
                    </div>
                  ))}
                </div>
              )}
            </section>
          )
        })}
      </div>
      {!stacks.length && (
        <Notice>{t(searching ? 'spools.no_match' : archived ? 'spools.no_archived' : 'spools.empty_list')}</Notice>
      )}
      {editing && (
        <SpoolForm
          spool={editing.spool}
          template={editing.template}
          busy={busy}
          act={act}
          close={() => setEditing(null)}
        />
      )}
      {weighing && <Weigh spool={weighing} busy={busy} act={act} close={() => setWeighing(null)} />}
    </Card>
  )
}

export const Spools = () => {
  const { data: lib, ok } = spools.use()
  const v = view(printer.use().data)
  const { busy, act } = useAct()
  // Remaining weights change while printing, so the page follows a print more closely.
  usePoll(refreshSpools, lib?.enabled && (lib.job.active || v.active) ? 5000 : 20000)
  return (
    <Page title="spools.title" sub="spools.subtitle">
      {!lib ? (
        <Notice>{t(ok ? 'spools.loading' : 'spools.offline')}</Notice>
      ) : !lib.available ? (
        <Warn>{tpl('spools.unavailable', { error: lib.error })}</Warn>
      ) : (
        <div class="grid gap-3.5">
          <Tracking lib={lib} busy={busy} act={act} />
          {lib.enabled && <InPrinter lib={lib} busy={busy} act={act} />}
          <Inventory lib={lib} busy={busy} act={act} />
          <Card>
            <CardHead icon="info" title="spools.recent" />
            <History lib={lib} events={lib.log.slice(0, 30)} spool />
          </Card>
        </div>
      )}
    </Page>
  )
}
