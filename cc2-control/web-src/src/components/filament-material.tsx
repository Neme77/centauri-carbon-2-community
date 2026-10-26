import { useState } from 'preact/hooks'
import { Dialog, DialogTitle } from '@/components/ui/dialog'
import { Button } from '@/components/ui/button'
import { Select } from '@/components/ui/field'
import { t } from '@/lib/i18n'
import { control } from '@/lib/api'
import { ask } from '@/lib/confirm'
import catalog from '@/lib/external-filament-catalog.json'

export const FilamentMaterial = ({
  filament,
  slot,
  onClose,
  onSaved,
}: {
  filament: any
  slot?: number
  onClose: () => void
  onSaved: (material: string, colour: string) => void
}) => {
  const maker = filament?.manufacturer || filament?.brand
  const initialBrand = Math.max(
    0,
    catalog.brands.findIndex(b => b.name === maker)
  )
  const currentType = filament?.type || filament?.filament_type
  const currentName = filament?.detailed_type || filament?.filament_name
  const candidates = catalog.brands[initialBrand].types
  const exactType = candidates.findIndex(x => x.name === currentType)
  const initialType = Math.max(
    0,
    exactType >= 0 ? exactType : candidates.findIndex(x => x.items.some(i => i.name === currentName))
  )
  const initialItem = Math.max(
    0,
    candidates[initialType].items.findIndex(x => x.name === currentName)
  )
  const [brand, setBrand] = useState(initialBrand),
    [type, setType] = useState(initialType),
    [item, setItem] = useState(initialItem)
  const originalColour = String(filament?.color || filament?.filament_color || '#FFFFFF').replace(/^0x/i, '#')
  const [colour, setColour] = useState(
    /^#[0-9a-f]{6}$/i.test(originalColour) ? originalColour.toUpperCase() : '#FFFFFF'
  )
  const [busy, setBusy] = useState(false)
  const types = catalog.brands[brand].types,
    items = types[type].items,
    selected = items[item]
  const supported = selected.min >= 120 && selected.max <= 320
  const submit = async (e: Event) => {
    e.preventDefault()
    if (busy || !supported || !(await ask(t('filament.save_confirm')))) return
    setBusy(true)
    try {
      const action = slot === undefined ? 'external:material' : `canvas:catalog:${slot}`
      if (await control(`${action}:${brand}:${type}:${item}:${colour.slice(1)}`)) {
        onSaved(selected.name, colour)
        onClose()
      }
    } finally {
      setBusy(false)
    }
  }
  return (
    <Dialog onClose={onClose}>
      <form onSubmit={submit} data-filament-material>
        <DialogTitle class="mb-4">{t('filament.edit')}</DialogTitle>
        <div class="grid gap-3">
          <label>
            {t('filament.brand')}
            <Select
              class="mt-1.5"
              aria-label={t('filament.brand')}
              value={brand}
              disabled={busy}
              onChange={e => {
                setBrand(+e.currentTarget.value)
                setType(0)
                setItem(0)
              }}
            >
              {catalog.brands.map((b, i) => (
                <option key={b.name} value={i}>
                  {b.name}
                </option>
              ))}
            </Select>
          </label>
          <label>
            {t('filament.type')}
            <Select
              class="mt-1.5"
              aria-label={t('filament.type')}
              value={type}
              disabled={busy}
              onChange={e => {
                setType(+e.currentTarget.value)
                setItem(0)
              }}
            >
              {types.map((x, i) => (
                <option key={x.name} value={i}>
                  {x.name}
                </option>
              ))}
            </Select>
          </label>
          <label>
            {t('filament.category')}
            <Select
              class="mt-1.5"
              aria-label={t('filament.category')}
              value={item}
              disabled={busy}
              onChange={e => setItem(+e.currentTarget.value)}
            >
              {items.map((x, i) => (
                <option key={x.name} value={i}>
                  {x.name}
                </option>
              ))}
            </Select>
          </label>
          <div class="text-xs text-muted">
            {selected.min}–{selected.max} °C
          </div>
          {!supported && <p class="text-warn">{t('filament.unsupported_temperature')}</p>}
          <label>
            {t('filament.colour')}
            <input
              class="mt-1.5 block h-11 w-full rounded-md border border-edge bg-field"
              type="color"
              aria-label={t('filament.colour')}
              value={colour}
              disabled={busy}
              onInput={e => setColour(e.currentTarget.value.toUpperCase())}
            />
          </label>
          <fieldset class="m-0 grid grid-cols-8 gap-2 border-0 p-0" aria-label={t('canvas.quick_colours')}>
            {catalog.colors.map(c => (
              <button
                key={c}
                type="button"
                class="h-8 rounded-md border-2 border-edge"
                style={{ backgroundColor: c, outline: c === colour ? '2px solid var(--cyan)' : undefined }}
                aria-label={c}
                aria-pressed={c === colour}
                disabled={busy}
                onClick={() => setColour(c)}
              />
            ))}
          </fieldset>
        </div>
        <div class="mt-5 flex justify-end gap-2">
          <Button disabled={busy} onClick={onClose}>
            {t('common.cancel')}
          </Button>
          <Button type="submit" variant="primary" disabled={busy || !supported}>
            {t('filament.save')}
          </Button>
        </div>
      </form>
    </Dialog>
  )
}
