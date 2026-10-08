import { useEffect, useMemo, useState } from 'preact/hooks'
import { cn } from '@/lib/utils'
import { Dialog } from '@/components/ui/dialog'
import { Button } from '@/components/ui/button'
import { Input, Select } from '@/components/ui/field'
import { Tag } from '@/components/ui/badge'
import { control, errText, notify } from '@/lib/api'
import { type Key, t, tpl } from '@/lib/i18n'
import { presets, printer, view } from '@/lib/state'
import {
  assignSpool,
  colourWord,
  densityFor,
  dismissSlot,
  EXTERNAL,
  fits,
  g,
  isLineOf,
  isLow,
  isSealed,
  MATERIALS,
  openChooser,
  percent,
  refreshSpools,
  type ChooserReason,
  type Spool,
  type SpoolFields,
  type SpoolLibrary,
  type SpoolSummary,
  type SpoolTray,
  saveSpool,
  slotOf,
  spoolChooser,
  spoolGrams,
  spoolLabel,
  spools,
  twinKey,
} from '@/lib/spools'

export const placeName = (slot: number) =>
  slot === EXTERNAL ? t('spools.external') : tpl('common.slot_n', { n: slot + 1 })

export const Swatch = ({ color, class: c }: { color: string; class?: string }) => (
  <span
    class={cn('inline-block size-4 shrink-0 rounded-full border border-white/30', c)}
    style={{ background: color || 'transparent' }}
  />
)

// Remaining filament as a bar and grams; amber below the spool's warning level.
export const SpoolMeter = ({ spool }: { spool: Spool }) => {
  const low = isLow(spool)
  return (
    <div class="grid gap-1">
      <div class="flex items-baseline justify-between gap-2 text-xs">
        <b class={cn('text-[13px]', low && 'text-amber')}>{g(spool.remaining)} g</b>
        <span class="text-muted">{tpl('spools.of_net', { net: g(spool.net) })}</span>
      </div>
      <div class="h-1.5 overflow-hidden rounded-full bg-edge">
        <i
          class={cn('block h-full rounded-full', low ? 'bg-amber' : 'bg-cyan')}
          style={{ width: `${percent(spool)}%` }}
        />
      </div>
    </div>
  )
}

/* ---- the spool form ---------------------------------------------------------------- */

export type SpoolDraft = Record<
  | 'name'
  | 'brand'
  | 'material'
  | 'color'
  | 'net'
  | 'remaining'
  | 'tare'
  | 'low'
  | 'diameter'
  | 'density'
  | 'price'
  | 'note',
  string
> & { densityTouched: boolean }

const num = (v: number) => (Number.isFinite(v) ? String(Math.round(v * 1000) / 1000) : '')

export const draftFrom = (
  s: Spool | null,
  tray?: SpoolTray | null,
  prefill?: { material: string; color: string } | null
): SpoolDraft => {
  if (s)
    return {
      name: s.name,
      brand: s.brand,
      material: s.material,
      color: s.color,
      net: num(s.net),
      remaining: num(s.remaining),
      tare: s.tare ? num(s.tare) : '',
      low: num(s.low),
      diameter: num(s.diameter),
      density: num(s.density),
      price: s.price ? num(s.price) : '',
      note: s.note,
      densityTouched: true,
    }
  // A tray reports a product line ("PLA Matte") besides its type ("PLA"): the spool keeps the line as its
  // material, which still fits the tray. Trays report a colour value only, so the name gets a colour word.
  const line = tray && isLineOf(tray.name, tray.type) ? tray.name : ''
  const material = prefill?.material || line || tray?.type || 'PLA'
  const brand = tray?.brand && !/^generic$/i.test(tray.brand) ? tray.brand : ''
  const color = (prefill?.color || tray?.color || '#2196F3').toUpperCase()
  const product = prefill ? material : tray?.name || material
  return {
    name: prefill || tray ? [brand, product, t(colourWord(color))].filter(Boolean).join(' ') : '',
    brand,
    material,
    color,
    net: '1000',
    remaining: '1000',
    tare: '',
    low: '100',
    diameter: '1.75',
    density: num(densityFor(material)),
    price: '',
    note: '',
    densityTouched: false,
  }
}

// Same limits as spools.h: printable text without quotes or backslashes, bounded numbers.
const textOk = (v: string, max: number) =>
  new TextEncoder().encode(v).length <= max &&
  ![...v].some(ch => {
    const c = ch.codePointAt(0) ?? 0
    return c < 32 || c === 34 || c === 92 || (c >= 127 && c <= 159) || c === 8232 || c === 8233
  })
const inRange = (v: string, min: number, max: number, optional = false) =>
  (optional && !v.trim()) || (/^\d+(\.\d+)?$/.test(v.trim()) && Number(v) >= min && Number(v) <= max)

export const draftProblem = (d: SpoolDraft, create: boolean): Key | null => {
  if (!d.material.trim() || !textOk(d.material.trim(), 32)) return 'spools.invalid_material'
  if (![d.name, d.brand, d.note].every(v => textOk(v.trim(), 96))) return 'spools.invalid_text'
  if (!/^#[0-9a-f]{6}$/i.test(d.color)) return 'spools.invalid_colour'
  const numbers =
    inRange(d.net, 1, 20000) &&
    (!create || inRange(d.remaining, 0, 20000)) &&
    inRange(d.tare, 0, 5000, true) &&
    inRange(d.low, 0, 20000, true) &&
    inRange(d.diameter, 1, 3.5) &&
    inRange(d.density, 0.5, 3) &&
    inRange(d.price, 0, 1e7, true)
  return numbers ? null : 'spools.invalid_number'
}

export const draftFields = (d: SpoolDraft, create: boolean): SpoolFields => ({
  name: d.name.trim(),
  brand: d.brand.trim(),
  material: d.material.trim(),
  color: d.color.toUpperCase(),
  net: Number(d.net),
  ...(create ? { remaining: Number(d.remaining) } : {}),
  tare: Number(d.tare || 0),
  low: Number(d.low || 0),
  diameter: Number(d.diameter),
  density: Number(d.density),
  price: Number(d.price || 0),
  note: d.note.trim(),
})

const Field = ({ label, children, class: c }: { label: Key; children: preact.ComponentChildren; class?: string }) => (
  // biome-ignore lint/a11y/noLabelWithoutControl: every caller passes the field's control as a child
  <label class={cn('grid content-start gap-1 text-xs', c)}>
    <span class="text-muted">{t(label)}</span>
    {children}
  </label>
)

export const SpoolEditor = ({
  draft,
  set,
  create,
}: {
  draft: SpoolDraft
  set: (d: SpoolDraft) => void
  create: boolean
}) => {
  const list = presets.use().list
  const materials = [...new Set([...list.map(p => String(p.name)), ...MATERIALS])]
  const change = (key: keyof SpoolDraft, value: string) => {
    const next = { ...draft, [key]: value }
    if (key === 'material' && !draft.densityTouched) next.density = num(densityFor(value))
    if (key === 'density') next.densityTouched = true
    // A new full spool: the remaining weight follows the net weight until it is edited.
    if (key === 'net' && create && draft.remaining === draft.net) next.remaining = value
    set(next)
  }
  const input = (key: keyof SpoolDraft) => ({
    value: draft[key] as string,
    onInput: (e: Event) => change(key, (e.currentTarget as HTMLInputElement).value),
  })
  return (
    <div class="grid gap-3 cc2-sm:grid-cols-6">
      <Field label="common.name" class="cc2-sm:col-span-6">
        <Input maxLength={96} placeholder={t('spools.name_placeholder')} {...input('name')} />
      </Field>
      <Field label="spools.brand" class="cc2-sm:col-span-3">
        <Input maxLength={96} {...input('brand')} />
      </Field>
      <Field label="canvas.material" class="cc2-sm:col-span-3">
        <Input maxLength={32} list="cc2-spool-materials" {...input('material')} />
        <datalist id="cc2-spool-materials">
          {materials.map(m => (
            <option key={m} value={m} />
          ))}
        </datalist>
      </Field>
      <Field label="canvas.filament_colour" class="cc2-sm:col-span-3">
        <span class="flex items-center gap-2">
          <input
            type="color"
            aria-label={t('canvas.filament_colour')}
            class="h-9 w-14 shrink-0 rounded-md border border-edge bg-field p-0.5"
            value={/^#[0-9a-f]{6}$/i.test(draft.color) ? draft.color.toLowerCase() : '#000000'}
            onInput={e => change('color', e.currentTarget.value.toUpperCase())}
          />
          <Input class="font-mono" maxLength={7} {...input('color')} />
        </span>
      </Field>
      <Field label="spools.diameter" class="cc2-sm:col-span-3">
        <Select value={draft.diameter} onChange={e => change('diameter', e.currentTarget.value)}>
          {['1.75', '2.85'].map(d => (
            <option key={d} value={d}>
              {d}
            </option>
          ))}
          {!['1.75', '2.85'].includes(draft.diameter) && <option value={draft.diameter}>{draft.diameter}</option>}
        </Select>
      </Field>
      <Field label="spools.net" class="cc2-sm:col-span-2">
        <Input type="number" inputMode="decimal" min="1" max="20000" step="1" {...input('net')} />
      </Field>
      {create && (
        <Field label="spools.remaining_g" class="cc2-sm:col-span-2">
          <Input type="number" inputMode="decimal" min="0" max="20000" step="1" {...input('remaining')} />
        </Field>
      )}
      <Field label="spools.tare" class={create ? 'cc2-sm:col-span-2' : 'cc2-sm:col-span-4'}>
        <Input
          type="number"
          inputMode="decimal"
          min="0"
          max="5000"
          step="1"
          placeholder={t('spools.tare_placeholder')}
          {...input('tare')}
        />
      </Field>
      {create && (
        <div class="flex flex-wrap gap-1.5 cc2-sm:col-span-6">
          {['250', '500', '750', '1000', '2000', '3000'].map(w => (
            <Button
              key={w}
              variant={draft.net === w ? 'active' : 'default'}
              class="min-h-7 px-2 py-0.5 text-xs"
              onClick={() => set({ ...draft, net: w, remaining: draft.remaining === draft.net ? w : draft.remaining })}
            >
              {w} g
            </Button>
          ))}
        </div>
      )}
      <Field label="spools.density" class="cc2-sm:col-span-2">
        <Input type="number" inputMode="decimal" min="0.5" max="3" step="0.01" {...input('density')} />
      </Field>
      <Field label="spools.low" class="cc2-sm:col-span-2">
        <Input type="number" inputMode="decimal" min="0" max="20000" step="1" {...input('low')} />
      </Field>
      <Field label="spools.price" class="cc2-sm:col-span-2">
        <Input type="number" inputMode="decimal" min="0" step="0.01" {...input('price')} />
      </Field>
      <Field label="spools.note" class="cc2-sm:col-span-6">
        <Input maxLength={96} {...input('note')} />
      </Field>
    </div>
  )
}

/* ---- which spool is in a tray --------------------------------------------------------- */

// Questions put off with "Not now" stay quiet until the tray reports something new.
const snoozed = new Set<string>()

// Opens the question of a tray when the printer reports new filament in it, on whatever page is open.
export const SpoolWatcher = () => {
  const summary = printer.use().data?.spools as SpoolSummary
  const { data } = spools.use()
  const chooser = spoolChooser.use()
  const questions = summary?.enabled ? summary.questions || [] : []
  const key = questions.join(',')
  useEffect(() => {
    if ((questions.length && spools.get().data?.revision !== summary?.revision) || (chooser.slot !== null && !data))
      void refreshSpools()
  }, [key, summary?.revision, chooser.slot])
  useEffect(() => {
    if (chooser.slot !== null || !data?.enabled) return
    const open = data.slots.find(
      s => s.question && questions.includes(s.slot) && !snoozed.has(`${s.slot}:${s.question_since}`)
    )
    if (open) openChooser(open.slot, 'question')
  }, [data, key, chooser.slot])
  return chooser.slot !== null && data ? (
    <SpoolChooser lib={data} slot={chooser.slot} reason={chooser.reason} prefill={chooser.prefill} />
  ) : null
}

// `count`: identical sealed spools in storage are interchangeable, so they are offered once.
type Option = { spool: Spool; hint: Key | null; where: number; count: number }

const SpoolChooser = ({
  lib,
  slot,
  reason,
  prefill,
}: {
  lib: SpoolLibrary
  slot: number
  reason: ChooserReason
  prefill: { material: string; color: string } | null
}) => {
  const state = lib.slots[slot]
  const tray = slot < EXTERNAL && state.printer && state.printer.status !== 0 ? state.printer : null
  const want = prefill ? { type: prefill.material, color: prefill.color } : tray
  const idle = view(printer.use().data).idle
  const options = useMemo(() => {
    const out: Option[] = []
    // Every spool counts once, also one that was folded into a twin's option.
    const seen = new Set<string>()
    const add = (spool: Spool | undefined | null, hint: Key | null) => {
      if (!spool || spool.archived || seen.has(spool.id)) return
      seen.add(spool.id)
      const where = slotOf(lib, spool.id)
      const twin =
        where < 0 && isSealed(spool)
          ? out.find(o => o.where < 0 && o.hint === hint && isSealed(o.spool) && twinKey(o.spool) === twinKey(spool))
          : null
      if (twin) twin.count++
      else out.push({ spool, hint, where, count: 1 })
    }
    add(
      lib.spools.find(s => s.id === state.spool),
      'spools.current'
    )
    add(
      lib.spools.find(s => s.id === state.last),
      'spools.was_here'
    )
    const free = lib.spools.filter(s => slotOf(lib, s.id) < 0)
    for (const s of free) if (want && fits(s, want)) add(s, 'spools.same_filament')
    for (const s of [...free].sort((a, b) => b.used - a.used)) add(s, null)
    for (const s of lib.spools) add(s, null)
    return out
  }, [lib, slot])
  // Preselect the tray's spool, else the one it held if the filament fits it, else a fitting spool.
  const last = options.find(o => o.hint === 'spools.was_here'),
    fitting = options.find(o => o.hint === 'spools.same_filament')
  const [choice, setChoice] = useState(
    () =>
      state.spool ||
      (last && (!want || fits(last.spool, want))
        ? last.spool.id
        : fitting
          ? fitting.spool.id
          : options.length
            ? ''
            : 'new')
  )
  const [all, setAll] = useState(false)
  const [busy, setBusy] = useState(false)
  const [draft, setDraft] = useState(() => draftFrom(null, tray, prefill))
  const chosen = options.find(o => o.spool.id === choice)?.spool || null
  // Writing the spool's filament to the tray keeps the printer and the slicer in step with the inventory.
  const preset = chosen
    ? presets
        .get()
        .list.find(
          p =>
            String(p.name).toLowerCase() === chosen.material.toLowerCase() ||
            chosen.material.toLowerCase().startsWith(String(p.name).toLowerCase())
        )
    : null
  const canWrite = Boolean(chosen && slot < EXTERNAL && reason !== 'material' && preset && idle && !fits(chosen, tray))
  const [write, setWrite] = useState(true)
  const shown = all ? options : options.slice(0, 6)
  const title: Key =
    reason === 'question'
      ? state.question === 'changed'
        ? 'spools.title_changed'
        : 'spools.title_inserted'
      : 'spools.title_choose'

  const close = (key = true) => {
    if (key && state.question) snoozed.add(`${slot}:${state.question_since}`)
    spoolChooser.set({ slot: null, prefill: null })
  }
  const run = async (fn: () => Promise<unknown>) => {
    if (busy) return
    setBusy(true)
    try {
      await fn()
      close()
      await refreshSpools()
    } catch (e) {
      notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
    } finally {
      setBusy(false)
    }
  }
  const confirm = () =>
    run(async () => {
      if (choice === 'new') {
        const problem = draftProblem(draft, true)
        if (problem) throw new Error(t(problem))
        await saveSpool({ ...draftFields(draft, true), slot })
        notify(
          tpl('spools.assigned_toast', { name: draft.name.trim() || draft.material.trim(), place: placeName(slot) })
        )
        return
      }
      if (!chosen) {
        await assignSpool(slot, '')
        notify(tpl('spools.emptied_toast', { place: placeName(slot) }))
        return
      }
      await assignSpool(slot, chosen.id)
      notify(tpl('spools.assigned_toast', { name: spoolLabel(chosen), place: placeName(slot) }))
      if (canWrite && write && preset) {
        const min = Number(preset.min || Math.max(120, preset.nozzle - 20)),
          max = Number(preset.max || Math.min(320, preset.nozzle + 20))
        await control(`canvas:material:${slot}:${preset.name}:${chosen.color.slice(1)}:${min}:${max}`)
      }
    })
  const radio = 'mt-0.5 shrink-0 accent-cyan'
  return (
    <Dialog onClose={() => !busy && close()} locked={busy} width={700}>
      <div class="text-xs text-muted">{t('spools.title')}</div>
      <h2 class="my-2 text-xl font-semibold">{tpl(title, { place: placeName(slot) })}</h2>
      {tray && (
        <div class="mb-3 flex flex-wrap items-center gap-2 rounded-md border border-edge bg-field/60 px-3 py-2 text-xs">
          <span class="text-muted">{t('spools.printer_reports')}</span>
          <Swatch color={tray.color} />
          <b>{[tray.brand, tray.name || tray.type].filter(Boolean).join(' ') || '—'}</b>
          <span class="font-mono text-muted">{tray.color}</span>
        </div>
      )}
      {state.question && state.question_mm > 0 && (
        <p class="mb-3 text-xs text-amber">{tpl('spools.question_usage', { mm: Math.round(state.question_mm) })}</p>
      )}
      <fieldset class="m-0 grid min-w-0 gap-2 border-0 p-0">
        <legend class="mb-2 p-0 text-[13px] font-semibold">{t('spools.which_spool')}</legend>
        {shown.map(({ spool: s, hint, where, count }) => (
          <label
            key={s.id}
            class={cn(
              'flex cursor-pointer items-start gap-3 rounded-lg border p-2.5',
              choice === s.id ? 'border-cyan bg-field' : 'border-edge'
            )}
          >
            <input
              type="radio"
              name="cc2-spool-choice"
              class={radio}
              checked={choice === s.id}
              onChange={() => setChoice(s.id)}
            />
            <Swatch color={s.color} class="mt-0.5 size-5" />
            <span class="grid min-w-0 flex-1 gap-0.5">
              <b class="text-sm [overflow-wrap:anywhere]">{spoolLabel(s)}</b>
              <span class="text-xs text-muted">
                {[s.brand && s.name ? s.brand : '', s.material, `${g(s.remaining)} g`].filter(Boolean).join(' · ')}
              </span>
            </span>
            <span class="flex shrink-0 flex-col items-end gap-1">
              {hint && <Tag tone={hint === 'spools.same_filament' ? 'ok' : 'default'}>{t(hint)}</Tag>}
              {count > 1 && <Tag>{tpl('spools.n_sealed', { n: count })}</Tag>}
              {where >= 0 && where !== slot && (
                <Tag tone="warning">{tpl('spools.moves_from', { place: placeName(where) })}</Tag>
              )}
            </span>
          </label>
        ))}
        {options.length > shown.length && (
          <Button variant="ghost" class="justify-self-start text-xs" onClick={() => setAll(true)}>
            {tpl('spools.show_all', { n: options.length })}
          </Button>
        )}
        <label
          class={cn(
            'flex cursor-pointer items-start gap-3 rounded-lg border p-2.5',
            choice === 'new' ? 'border-cyan bg-field' : 'border-edge'
          )}
        >
          <input
            type="radio"
            name="cc2-spool-choice"
            class={radio}
            checked={choice === 'new'}
            onChange={() => setChoice('new')}
          />
          <span class="grid gap-0.5">
            <b class="text-sm">{t('spools.new_spool')}</b>
            <span class="text-xs text-muted">{t('spools.new_spool_hint')}</span>
          </span>
        </label>
        {choice === 'new' && (
          <div class="rounded-lg border border-edge p-3">
            <SpoolEditor draft={draft} set={setDraft} create />
          </div>
        )}
        {(reason !== 'question' || slot === EXTERNAL) && (
          <label
            class={cn(
              'flex cursor-pointer items-start gap-3 rounded-lg border p-2.5',
              choice === '' ? 'border-cyan bg-field' : 'border-edge'
            )}
          >
            <input
              type="radio"
              name="cc2-spool-choice"
              class={radio}
              checked={choice === ''}
              onChange={() => setChoice('')}
            />
            <span class="grid gap-0.5">
              <b class="text-sm">{t('spools.no_spool_option')}</b>
              <span class="text-xs text-muted">{t('spools.no_spool_hint')}</span>
            </span>
          </label>
        )}
      </fieldset>
      {canWrite && (
        <label class="mt-3 flex items-center gap-2 text-[13px]">
          <input type="checkbox" checked={write} onChange={e => setWrite(e.currentTarget.checked)} />
          {tpl('spools.write_printer', { place: placeName(slot) })}
        </label>
      )}
      <div class="mt-5 flex flex-wrap items-center justify-end gap-2.5">
        {reason === 'question' && slot < EXTERNAL && (
          <Button
            variant="ghost"
            class="mr-auto text-xs"
            disabled={busy}
            onClick={() =>
              run(async () => {
                await dismissSlot(slot)
                notify(tpl('spools.untracked_toast', { place: placeName(slot) }))
              })
            }
          >
            {t('spools.dont_track')}
          </Button>
        )}
        <Button disabled={busy} onClick={() => close()}>
          {t(reason === 'question' ? 'spools.not_now' : 'common.cancel')}
        </Button>
        <Button
          variant="primary"
          disabled={busy || (choice === '' && !state.spool && reason === 'question')}
          onClick={confirm}
        >
          {t('common.confirm')}
        </Button>
      </div>
    </Dialog>
  )
}

// The grams a print needs from a spool, from the slicer's length for its tool.
export const needOf = (spool: Spool, mm?: number) => (Number(mm) > 0 ? spoolGrams(spool, Number(mm)) : null)
