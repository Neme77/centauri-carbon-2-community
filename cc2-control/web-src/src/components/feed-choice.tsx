import type { ComponentChildren } from 'preact'
import { useState } from 'preact/hooks'
import { Button } from '@/components/ui/button'
import { canvasHex } from '@/lib/canvas'
import { type Key, t, tpl } from '@/lib/i18n'
import type { FilamentLabel } from '@/lib/queue'
import { byWheel, colourName, colourWord, type SpoolLibrary } from '@/lib/spools'

// Missing Canvas colours are unknown, not the shared helper's decorative fallback.
export const slotColour = (tray: any, index: number) => {
  const value = tray?.filament_color
  if (value === undefined || value === null || value === '') return null
  if (Array.isArray(value) && value.length >= 3 && value.slice(0, 3).every(Number.isFinite))
    return canvasHex(value, index)
  if (typeof value === 'number' && Number.isFinite(value)) return canvasHex(value, index)
  if (typeof value === 'string' && /^(?:#?[0-9a-f]{6}(?:[0-9a-f]{2})?|0x[0-9a-f]{6,8})$/i.test(value.trim()))
    return canvasHex(value, index)
  return null
}

const labelOf = (tray: any) => (tray && (tray.filament_name || tray.filament_type)) || ''

// A slot the Canvas reports as empty (status 0). It keeps the label of its last spool, which is the filament a
// job bound to that slot takes, so a label alone does not tell an empty slot from a loaded one.
export const slotEmpty = (tray: any) => tray?.status !== undefined && tray?.status !== null && Number(tray.status) === 0

// What a slot holds as its label names it; without a label, whether it is empty or holds a filament the
// printer does not report.
export const slotLabel = (tray: any) =>
  labelOf(tray) || (slotEmpty(tray) ? t('canvas.state_empty') : t('print.not_reported'))

// Under the label of an empty slot: that the spool it names is no longer in it.
export const slotEmptyNote = (tray: any) => (labelOf(tray) && slotEmpty(tray) ? t('canvas.state_empty') : '')

// The filaments of the spool inventory, as it files them: brand, then material, then colour. A queued job can
// be bound to one although no slot holds it. The job keeps the material and the colour, which is what the
// label of a Canvas slot can say (the material is the product line a slot names such a spool with); the brand
// is the way to the filament here.
export type StockFilament = FilamentLabel & { key: string; brand: string; label: string }
export type StockKind = { key: string; label: string; colours: StockFilament[] }
export type StockBrand = { key: string; label: string; kinds: StockKind[] }
export const stockBrands = (lib: SpoolLibrary | null | undefined): StockBrand[] => {
  if (!lib?.available || !lib.enabled || !Array.isArray(lib.spools)) return []
  const brands = new Map<string, StockBrand & { spools: number }>()
  for (const s of lib.spools) {
    const name = s.material.trim(),
      color = s.color.toUpperCase(),
      brandKey = s.brand.trim().toLowerCase(),
      kindKey = name.toLowerCase()
    if (s.archived || !name || !/^#[0-9A-F]{6}$/.test(color)) continue
    const brand = brands.get(brandKey) ?? { key: brandKey, label: s.brand.trim(), kinds: [], spools: 0 }
    brands.set(brandKey, brand)
    brand.spools++
    const kind = brand.kinds.find(k => k.key === kindKey) ?? { key: kindKey, label: name, colours: [] }
    if (!brand.kinds.includes(kind)) brand.kinds.push(kind)
    const key = `${brandKey}|${kindKey}|${color}`
    if (!kind.colours.some(f => f.key === key))
      kind.colours.push({
        key,
        brand: brand.label,
        type: '',
        name,
        color,
        label: colourName(s) || t(colourWord(color)),
      })
  }
  // The brand with most spools first, spools without a brand last; materials by name; colours as the inventory
  // orders them, and two that read alike under one material are told apart by their value.
  return [...brands.values()]
    .sort((a, b) => Number(!a.key) - Number(!b.key) || b.spools - a.spools || a.label.localeCompare(b.label))
    .map(brand => ({
      key: brand.key,
      label: brand.label,
      kinds: brand.kinds
        .sort((a, b) => a.label.localeCompare(b.label))
        .map(kind => {
          const colours = kind.colours.sort((a, b) => byWheel(a.color, b.color) || a.label.localeCompare(b.label))
          const same = (f: StockFilament) => colours.filter(o => o.label === f.label).length > 1
          return { ...kind, colours: colours.map(f => (same(f) ? { ...f, label: `${f.label} · ${f.color}` } : f)) }
        }),
    }))
}

// The choice for one tool: a Canvas slot ("0".."3"), or a filament of the inventory: "x:" and its key, or
// "x:" alone while its brand, material and colour are still being chosen.
export const isSlot = (value: string | undefined) => /^[0-3]$/.test(value ?? '')
export const isStock = (value: string | undefined) => (value ?? '').startsWith('x:')
const stockPath = (stock: StockBrand[], value: string | undefined) => {
  for (const brand of stock)
    for (const kind of brand.kinds)
      for (const filament of kind.colours) if (`x:${filament.key}` === value) return { brand, kind, filament }
  return null
}
export const stockOf = (stock: StockBrand[], value: string | undefined) => stockPath(stock, value)?.filament || null
const brandLabel = (brand: StockBrand) => brand.label || t('spools.other_brands')

const Square = ({ color }: { color: string }) => (
  <span class="inline-block size-4 shrink-0 rounded border border-edge" style={{ background: color }} />
)

// One level of the inventory: its name and the choices as a row of buttons, one of which is taken.
const Level = ({ label, level, children }: { label: Key; level: string; children: ComponentChildren }) => (
  <div class="grid gap-1 sm:grid-cols-[110px_minmax(0,1fr)] sm:items-baseline">
    <span class="text-muted">{t(label)}</span>
    <div role="radiogroup" aria-label={t(label)} class="flex flex-wrap gap-1.5" data-stock-level={level}>
      {children}
    </div>
  </div>
)
const Choice = ({
  selected,
  children,
  ...rest
}: { selected: boolean; children: ComponentChildren } & preact.JSX.ButtonHTMLAttributes<HTMLButtonElement>) => (
  <Button variant={selected ? 'active' : 'default'} role="radio" aria-checked={selected} class="text-xs" {...rest}>
    {children}
  </Button>
)

// The way to a filament of the inventory: the brand first, then one of its materials, then one of that
// material's colours. A step with a single choice is taken at once. Taking another brand or material starts
// the steps below it again, so what is highlighted is always the choice made so far.
const StockLevels = ({
  stock,
  value,
  onChange,
  disabled,
}: {
  stock: StockBrand[]
  value: string
  onChange: (value: string) => void
  disabled?: boolean
}) => {
  const chosen = stockPath(stock, value)
  const only = <T,>(list: T[]) => (list.length === 1 ? list[0] : null)
  const [at, setAt] = useState(() => {
    const brand = chosen?.brand ?? only(stock)
    return { brand: brand?.key ?? null, kind: (chosen?.kind ?? (brand && only(brand.kinds)))?.key ?? null }
  })
  const brand = stock.find(b => b.key === at.brand) ?? null
  const kind = brand?.kinds.find(k => k.key === at.kind) ?? null
  const move = (next: { brand: string | null; kind: string | null }) => {
    if (next.brand === at.brand && next.kind === at.kind) return
    setAt(next)
    if (chosen) onChange('x:')
  }
  return (
    <div class="mt-2 grid gap-2 border-t border-edge pt-2" data-stock-levels>
      <Level label="spools.brand" level="brand">
        {stock.map(b => (
          <Choice
            key={b.key}
            selected={b.key === at.brand}
            disabled={disabled}
            onClick={() => b.key !== at.brand && move({ brand: b.key, kind: only(b.kinds)?.key ?? null })}
            data-stock-brand={b.key}
          >
            {brandLabel(b)}
          </Choice>
        ))}
      </Level>
      {brand && (
        <Level label="canvas.material" level="kind">
          {brand.kinds.map(k => (
            <Choice
              key={k.key}
              selected={k.key === at.kind}
              disabled={disabled}
              onClick={() => move({ brand: brand.key, kind: k.key })}
              data-stock-kind={k.key}
            >
              {k.label}
            </Choice>
          ))}
        </Level>
      )}
      {kind && (
        <Level label="filament.colour" level="colour">
          {kind.colours.map(f => (
            <Choice
              key={f.key}
              selected={f.key === chosen?.filament.key}
              disabled={disabled}
              onClick={() => onChange(`x:${f.key}`)}
              data-stock-colour={f.color}
            >
              <Square color={f.color} />
              {f.label}
            </Choice>
          ))}
        </Level>
      )}
    </div>
  )
}

// Where one tool of a print takes its filament: one of the four Canvas slots, as the printer labels them, or,
// for a queued job (`stock`), a filament that is in no slot: the job then waits until a slot holds it.
// `stockOff` keeps that choice in place but closed, for a print that starts now.
export const FeedChoice = ({
  tool,
  name,
  trays,
  value,
  onChange,
  disabled,
  dim,
  stock = [],
  stockOff,
}: {
  tool: number
  name: string
  trays: any[]
  value: string
  onChange: (value: string) => void
  disabled?: boolean
  dim?: boolean
  stock?: StockBrand[]
  stockOff?: boolean
}) => {
  const tray = (i: number) => trays.find(x => x && Number(x.tray_id) === i)
  const waiting = stockPath(stock, value)
  const closed = dim || stockOff
  const card = (selected: boolean, off = dim) =>
    `rounded-lg border p-2 text-xs ${selected && !off ? 'border-cyan bg-cyan/10' : 'border-edge'}`
  return (
    <>
      <div class="grid grid-cols-2 gap-2 sm:grid-cols-4">
        {[0, 1, 2, 3].map(i => {
          const slot = tray(i)
          const color = slotColour(slot, i)
          const selected = value === String(i)
          const empty = slotEmpty(slot)
          // An unlabelled slot says what it is on the label line already
          const note = empty ? slotEmptyNote(slot) : color || (labelOf(slot) ? t('print.not_reported') : '')
          return (
            <label
              key={i}
              class={`flex min-w-0 items-start gap-2 ${card(selected)} ${dim ? 'opacity-50' : 'cursor-pointer'}`}
            >
              <input
                type="radio"
                name={name}
                value={String(i)}
                checked={selected}
                disabled={disabled}
                onChange={e => onChange(e.currentTarget.value)}
                class="mt-0.5 shrink-0"
              />
              <span
                class="grid min-w-0 gap-1 [overflow-wrap:anywhere]"
                data-feed-slot={i}
                data-feed-slot-state={empty ? 'empty' : labelOf(slot) ? 'labelled' : 'unknown'}
              >
                <strong>{tpl('common.slot_n', { n: i + 1 })}</strong>
                <span class="flex items-center gap-1.5">
                  {color && <Square color={color} />}
                  <span data-feed-slot-label>{slotLabel(slot)}</span>
                </span>
                {note && (
                  <span class="text-muted" data-feed-slot-note>
                    {note}
                  </span>
                )}
              </span>
            </label>
          )
        })}
      </div>
      {stock.length > 0 && (
        <fieldset
          class={`m-0 mt-2 min-w-0 ${card(isStock(value), closed)}`}
          aria-label={t('queue.filament_stock_pick')}
          data-feed-stock={tool}
        >
          <div class={`flex flex-wrap items-center gap-x-3 gap-y-1.5 ${closed ? 'opacity-50' : ''}`}>
            <label class={`flex items-center gap-2 ${closed ? '' : 'cursor-pointer'}`}>
              <input
                type="radio"
                name={name}
                value="stock"
                checked={isStock(value)}
                disabled={disabled || stockOff}
                onChange={() => onChange('x:')}
                class="shrink-0"
              />
              <strong>{t('queue.filament_stock')}</strong>
            </label>
            {isStock(value) && (
              <span class="flex min-w-0 items-center gap-1.5 [overflow-wrap:anywhere]" data-stock-choice>
                {waiting ? (
                  <>
                    <Square color={waiting.filament.color} />
                    {[waiting.brand.label, waiting.kind.label, waiting.filament.label].filter(Boolean).join(' · ')}
                  </>
                ) : (
                  <span class="text-muted">{t('common.not_set')}</span>
                )}
              </span>
            )}
          </div>
          {isStock(value) && (
            <StockLevels stock={stock} value={value} onChange={onChange} disabled={disabled || stockOff} />
          )}
          <div class="mt-2 text-muted">{t('queue.filament_stock_hint')}</div>
        </fieldset>
      )}
    </>
  )
}
