import { InfoHint } from '@/components/info-hint'
import type { ComponentChildren } from 'preact'
import { useEffect, useRef, useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Tag } from '@/components/ui/badge'
import { Input, Select } from '@/components/ui/field'
import { Notice, Warn } from '@/components/shared'
import { errText, notify } from '@/lib/api'
import { ask } from '@/lib/confirm'
import { t, tpl } from '@/lib/i18n'
import { usePoll } from '@/lib/poll'
import {
  clearMeasures,
  deleteMeasure,
  deleteNozzle,
  editMeasure,
  deletePlate,
  editPlate,
  findMeasure,
  type Measure,
  measurePlate,
  meshProfile,
  meshRange,
  mountPlate,
  type Nozzle,
  nozzleName,
  type Plate,
  type PlateLibrary,
  plateBase,
  plates,
  refreshPlates,
  saveNozzle,
  savePlate,
  createPlate,
  calibrationForm,
  selectNozzle,
  tempOk,
  restartForBlends,
  setMeshMatch,
  unmountPlate,
  zText,
} from '@/lib/plates'
import { openPage, printer, view, zoffset } from '@/lib/state'

const savedOn = (s: number) =>
  s > 0
    ? new Date(s * 1000).toLocaleString(undefined, {
        year: 'numeric',
        month: '2-digit',
        day: '2-digit',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
        timeZoneName: 'short',
      })
    : '—'
// The backend's limits: 1-64 UTF-8 bytes without quotes, backslashes, C0/C1 controls or the
// Unicode line separators; |z| <= 1 mm.
const forbidden = (c: number) => c < 32 || c === 34 || c === 92 || (c >= 127 && c <= 159) || c === 8232 || c === 8233
const nameOk = (name: string) =>
  name.length > 0 &&
  new TextEncoder().encode(name).length <= 64 &&
  ![...name].some(ch => forbidden(ch.codePointAt(0) ?? 0))
const decimalOk = (text: string, limit: number) =>
  /^[+-]?\d*\.?\d+$/.test(text.trim()) && Math.abs(Number(text)) <= limit
const zOk = (text: string) => decimalOk(text, 1)

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
      void refreshPlates()
    }
  }
  return { busy, act }
}

// The printer must restart when the mesh is not in its slot: confirm, then send the REBOOT line.
// `m`: mount with that measurement rather than the one the plate mounted with last.
async function mount(p: Plate, m?: Measure) {
  try {
    await mountPlate(p.id, false, m?.id)
    notify(tpl('plates.mounted_toast', { name: p.name }))
  } catch (e: any) {
    if (!e?.data?.reboot_required) throw e
    const question = m
      ? tpl('plates.write_measure_confirm', { name: p.name, side: p.side, temp: m.temp })
      : tpl('plates.reboot_confirm', { name: p.name, side: p.side })
    if (!(await ask(question, true))) return
    await mountPlate(p.id, true, m?.id)
    plates.set({ rebooting: true })
    notify(t('plates.restart_requested'))
  }
}

export const NozzleSelect = ({
  lib,
  value,
  onChange,
  disabled,
}: {
  lib: PlateLibrary
  value: string
  onChange: (id: string) => void
  disabled?: boolean
}) => (
  <Select value={value} disabled={disabled} onChange={e => onChange(e.currentTarget.value)}>
    <option value="">{t('common.not_set')}</option>
    {lib.nozzles.map(n => (
      <option key={n.id} value={n.id}>
        {n.name}
      </option>
    ))}
  </Select>
)

// A small "⋯" menu for the less frequent actions of a row or card; closes after a choice.
const More = ({ children }: { children: ComponentChildren }) => {
  const box = useRef<HTMLDetailsElement>(null)
  const [pos, setPos] = useState<{ top?: number; bottom?: number; right: number } | null>(null)
  // The panel is placed on the screen (fixed), so a scrolling list around the "⋯" does not cut it off.
  const place = () => {
    const r = box.current?.querySelector('summary')?.getBoundingClientRect()
    if (!box.current?.open || !r) return setPos(null)
    // Opens upwards when there is no room for it under the button (it would otherwise leave the screen).
    const right = window.innerWidth - r.right
    setPos(
      window.innerHeight - r.bottom < 170
        ? { bottom: window.innerHeight - r.top + 4, right }
        : { top: r.bottom + 4, right }
    )
  }
  useEffect(() => {
    if (!pos) return
    const close = (e: Event) => {
      if (!box.current?.contains(e.target as Node)) box.current?.removeAttribute('open')
    }
    const shut = () => box.current?.removeAttribute('open')
    document.addEventListener('pointerdown', close)
    window.addEventListener('scroll', shut, true)
    return () => {
      document.removeEventListener('pointerdown', close)
      window.removeEventListener('scroll', shut, true)
    }
  }, [pos])
  return (
    <details ref={box} class="relative" data-more onToggle={place}>
      <summary
        class="grid min-h-7 cursor-pointer place-items-center rounded-md border border-edge px-2 text-sm select-none [&::-webkit-details-marker]:hidden"
        aria-label={t('plates.more_actions')}
        title={t('plates.more_actions')}
      >
        ⋯
      </summary>
      {pos && (
        <div
          class="fixed z-50 grid min-w-44 gap-1 rounded-md border border-edge bg-panel p-1.5 shadow-lg [&_button]:w-full [&_button]:justify-start"
          style={{
            top: pos.top == null ? undefined : `${pos.top}px`,
            bottom: pos.bottom == null ? undefined : `${pos.bottom}px`,
            right: `${pos.right}px`,
          }}
          role="menu"
          tabindex={-1}
          onClick={() => box.current?.removeAttribute('open')}
          onKeyDown={e => {
            if (e.key === 'Escape') box.current?.removeAttribute('open')
          }}
        >
          {children}
        </div>
      )}
    </details>
  )
}

// Below a list that holds more than fits: how many entries there are and that the list scrolls.
const MoreBelow = ({ count, shown }: { count: number; shown: number }) =>
  count > shown ? (
    <p class="mt-1 flex items-center justify-center gap-1 text-xs text-cyan" data-more-below>
      <span aria-hidden="true">↓</span>
      {tpl('plates.more_below', { n: count })}
    </p>
  ) : null

const MeasureRow = ({
  p,
  m,
  lib,
  idle,
  locked,
  act,
}: {
  p: Plate
  m: Measure
  lib: PlateLibrary
  idle: boolean
  locked: boolean
  act: Act
}) => {
  const idleOnly = idle ? undefined : t('common.available_when_idle')
  // What the measurement records can be corrected, e.g. the nozzle of one made before nozzles were listed.
  const [editing, setEditing] = useState(false)
  const [temp, setTemp] = useState(String(m.temp))
  const [nozzle, setNozzle] = useState(m.nozzle)
  const remove = () =>
    act(async () => {
      if (!(await ask(tpl('plates.delete_measure_confirm', { name: p.name, temp: m.temp }), true))) return
      await deleteMeasure(m.id)
      notify(t('plates.measure_deleted'))
    })
  const save = () =>
    act(async () => {
      if (!tempOk(temp)) return notify(t('common.invalid_bed_temperature'), 'error')
      await editMeasure(m.id, Number(temp), nozzle)
      setEditing(false)
      notify(t('plates.measure_updated'))
    })
  if (editing)
    return (
      <div
        class="flex flex-wrap items-end gap-2 rounded-md border border-cyan px-2.5 py-1.5 text-xs"
        data-measure={m.id}
      >
        <label class="grid gap-1">
          {t('common.bed_temperature_c')}
          <Input
            class="w-24"
            type="number"
            min="30"
            max="110"
            step="1"
            value={temp}
            onInput={e => setTemp(e.currentTarget.value)}
          />
        </label>
        <label class="grid min-w-40 flex-1 gap-1">
          {t('common.nozzle')}
          <NozzleSelect lib={lib} value={nozzle} onChange={setNozzle} />
        </label>
        <Button variant="primary" class="min-h-9 px-3 text-xs" disabled={locked} onClick={save}>
          {t('plates.save')}
        </Button>
        <Button class="min-h-9 px-3 text-xs" onClick={() => setEditing(false)}>
          {t('common.cancel')}
        </Button>
      </div>
    )
  return (
    <div
      class="flex flex-wrap items-center gap-x-3 gap-y-1.5 rounded-md border border-edge px-2.5 py-1.5 text-xs"
      data-measure={m.id}
    >
      <b class="text-sm text-fg">{m.temp} °C</b>
      <span class="text-muted">{nozzleName(lib, m.nozzle) || t('plates.nozzle_unknown')}</span>
      <span class="text-muted">
        {t('plates.mesh_range')}: <b class="text-fg">{meshRange(m.mesh).toFixed(3)} mm</b>
      </span>
      <span class="text-muted">{tpl('plates.saved_on', { date: savedOn(m.measured) })}</span>
      {m.slot ? (
        <Tag tone="ok">{t('plates.in_slot')}</Tag>
      ) : m.profile ? (
        <Tag>{t('plates.in_printer_profile')}</Tag>
      ) : (
        <Tag tone="warning">{t('plates.library_only')}</Tag>
      )}
      <span class="ml-auto flex items-center gap-1.5">
        <Button
          class="min-h-7 px-2 text-xs"
          onClick={() => {
            meshProfile.set({ profile: `measure:${m.id}` })
            openPage('bed')
          }}
        >
          {t('plates.view_mesh')}
        </Button>
        <More>
          <Button
            class="min-h-8 px-2 text-xs"
            disabled={locked}
            onClick={() => {
              setTemp(String(m.temp))
              setNozzle(m.nozzle)
              setEditing(true)
            }}
          >
            {t('plates.edit_measure')}
          </Button>
          {!m.slot && (
            <Button
              class="min-h-8 px-2 text-xs"
              disabled={locked || !idle}
              title={idleOnly}
              onClick={() => act(() => mount(p, m))}
            >
              {t('plates.write_to_slot')}
            </Button>
          )}
          {p.measures.length > 1 && (
            <Button variant="danger" class="min-h-8 px-2 text-xs" disabled={locked} onClick={remove}>
              {t('common.delete')}
            </Button>
          )}
        </More>
      </span>
    </div>
  )
}

// Keeps the two faces of one plate next to each other (A before B) without reordering anything else.
function groupSides(plates: Plate[]): Plate[] {
  const out: Plate[] = []
  for (const p of plates) {
    if (out.includes(p)) continue
    const twin = plates.find(q => q !== p && q.name === p.name && q.side !== p.side && !out.includes(q))
    if (twin && p.side === 'B') out.push(twin, p)
    else if (twin) out.push(p, twin)
    else out.push(p)
  }
  return out
}

const PlateCard = ({
  p,
  lib,
  idle,
  busy,
  act,
}: {
  p: Plate
  lib: PlateLibrary
  idle: boolean
  busy: boolean
  act: Act
}) => {
  const { v: live, reference } = zoffset.use()
  const [editing, setEditing] = useState(false)
  const [name, setName] = useState(p.name)
  const [z, setZ] = useState(p.z_offset.toFixed(3))
  const base = plateBase(p)
  const [temp, setTemp] = useState(String(base?.temp ?? 60))
  const mounted = lib.current === p.id && !lib.pending
  const locked = busy || Boolean(lib.pending) || !lib.available
  const adjustment = mounted && live !== null && reference !== null ? live - reference : 0
  const idleOnly = idle ? undefined : t('common.available_when_idle')
  const save = () =>
    act(async () => {
      if (!nameOk(name.trim())) return notify(t('plates.invalid_name'), 'error')
      if (!zOk(z)) return notify(t('plates.invalid_z'), 'error')
      await editPlate(p.id, name.trim(), Number(z))
      setEditing(false)
      notify(t('plates.updated_toast'))
    })
  // Empties the plate on purpose (a function of its own; deleting one measurement still keeps the last one).
  const clearAll = () =>
    act(async () => {
      if (!(await ask(tpl('plates.measures_clear_confirm', { n: p.measures.length, name: p.name }), true))) return
      await clearMeasures(p.id)
      notify(t('plates.measures_cleared_toast'))
    })
  // The other face of the same physical plate: same name and Z as a starting point, calibrated on its own.
  const other = p.side === 'B' ? 'A' : 'B'
  const hasOther = lib.plates.some(q => q.name === p.name && q.side === other)
  const addSide = () =>
    act(async () => {
      await createPlate(other, p.name, p.z_offset)
      notify(tpl('plates.other_side_added', { name: p.name, side: other }))
    })
  const remove = () =>
    act(async () => {
      if (!(await ask(tpl('plates.delete_confirm', { name: p.name }), true))) return
      await deletePlate(p.id)
      notify(t('plates.deleted_toast'))
    })
  return (
    <div class="flex flex-col rounded-lg border border-edge bg-field/40 p-3" data-plate={p.id}>
      <div class="flex flex-wrap items-center gap-2">
        <strong class="mr-auto min-w-0 text-sm [overflow-wrap:anywhere]">{p.name}</strong>
        <Tag>{tpl('plates.side_n', { side: p.side })}</Tag>
        {mounted && <Tag tone="ok">{t('plates.mounted')}</Tag>}
        {p.in_printer && <Tag>{t('plates.in_printer')}</Tag>}
      </div>
      <div class="mt-2 text-xs text-muted">
        {t('plates.z_offset')}: <b class="text-fg">{zText(p.z_offset)} mm</b>
      </div>
      <fieldset
        class="mt-2 grid max-h-[13rem] min-w-0 gap-1.5 overflow-y-auto pr-1 [scrollbar-width:thin]"
        aria-label={t('plates.measures')}
        data-measure-list
      >
        {!p.measures.length && <Notice>{t('plates.not_calibrated')}</Notice>}
        {p.measures.map(m => (
          <MeasureRow key={m.id} p={p} m={m} lib={lib} idle={idle} locked={locked} act={act} />
        ))}
      </fieldset>
      <MoreBelow count={p.measures.length} shown={2} />
      {mounted && !p.in_printer && !lib.pending && (
        <Warn>
          {tpl('plates.mesh_differs', { side: p.side })}
          <div class="mt-2 flex flex-wrap items-end gap-2">
            <label class="grid gap-1 text-xs">
              {t('common.bed_temperature_c')}
              <Input
                class="w-24"
                type="number"
                min="30"
                max="110"
                step="1"
                value={temp}
                onInput={e => setTemp(e.currentTarget.value)}
              />
            </label>
            <Button
              class="text-xs"
              disabled={locked || !idle || !base}
              title={idleOnly}
              onClick={() =>
                act(async () => {
                  if (!tempOk(temp)) return notify(t('common.invalid_bed_temperature'), 'error')
                  await measurePlate(p.id, Number(temp), lib.nozzle)
                  notify(t('plates.updated_toast'))
                })
              }
            >
              {t('plates.save_measure')}
            </Button>
            <Button class="text-xs" disabled={locked || !idle} title={idleOnly} onClick={() => act(() => mount(p))}>
              {t('plates.write_to_printer')}
            </Button>
          </div>
        </Warn>
      )}
      {editing ? (
        <div class="mt-3 grid gap-2 cc2-sm:grid-cols-[1fr_9rem_auto]">
          <label class="grid gap-1 text-xs">
            {t('common.name')}
            <Input value={name} maxLength={64} onInput={e => setName(e.currentTarget.value)} />
          </label>
          <label class="grid gap-1 text-xs">
            {t('plates.z_offset_mm')}
            <Input type="number" step="0.005" min="-1" max="1" value={z} onInput={e => setZ(e.currentTarget.value)} />
          </label>
          <div class="flex items-end gap-2">
            <Button variant="primary" disabled={locked} onClick={save}>
              {t('plates.save')}
            </Button>
            <Button disabled={busy} onClick={() => setEditing(false)}>
              {t('common.cancel')}
            </Button>
          </div>
          {Math.abs(adjustment) >= 0.0005 && (
            <Button
              class="justify-self-start text-xs cc2-sm:col-span-3"
              onClick={() => setZ((Number(z) + adjustment).toFixed(3))}
            >
              {tpl('plates.add_live_adjustment', { delta: zText(adjustment) })}
            </Button>
          )}
        </div>
      ) : (
        <div class="mt-auto flex flex-wrap items-center gap-2 pt-3">
          {!mounted && (
            <Button
              variant="primary"
              class="text-xs"
              disabled={locked || !idle || !base}
              title={idleOnly}
              onClick={() => act(() => mount(p))}
            >
              {t('plates.mount')}
            </Button>
          )}
          <Button
            class="text-xs"
            disabled={locked || !idle}
            onClick={() => {
              calibrationForm.set({
                side: p.side === 'B' ? 'default1' : 'default',
                target: p.id,
                temp: String(base?.temp ?? 60),
                nozzle: lib.nozzle,
              })
              openPage('bed')
            }}
          >
            {t('plates.calibrate_plate')}
          </Button>
          {!hasOther && (
            <Button class="text-xs" disabled={locked || !idle} title={idleOnly} onClick={addSide} data-add-side>
              {tpl('plates.add_other_side', { side: other })}
            </Button>
          )}
          <span class="ml-auto">
            <More>
              <Button
                class="min-h-8 px-2 text-xs"
                disabled={locked}
                onClick={() => {
                  setName(p.name)
                  setZ(p.z_offset.toFixed(3))
                  setEditing(true)
                }}
              >
                {t('plates.edit')}
              </Button>
              {p.measures.length > 0 && (
                <Button
                  variant="danger"
                  class="min-h-8 px-2 text-xs"
                  disabled={locked || mounted}
                  title={mounted ? t('plates.measures_clear_mounted_hint') : undefined}
                  onClick={clearAll}
                  data-clear-measures
                >
                  {t('plates.measures_clear')}
                </Button>
              )}
              <Button variant="danger" class="min-h-8 px-2 text-xs" disabled={locked} onClick={remove}>
                {t('common.delete')}
              </Button>
            </More>
          </span>
        </div>
      )}
    </div>
  )
}

// The nozzles that may be on the printer: the installed one adds its Z correction to the mounted plate.
const NozzlesCard = ({ lib, busy, act }: { lib: PlateLibrary; busy: boolean; act: Act }) => {
  const empty = { id: '', name: '', diameter: '0.40', z: '0.000' }
  const [form, setForm] = useState(empty)
  const locked = busy || Boolean(lib.pending) || !lib.available
  const edit = (n: Nozzle) =>
    setForm({ id: n.id, name: n.name, diameter: n.diameter.toFixed(2), z: n.z_offset.toFixed(3) })
  const save = () =>
    act(async () => {
      if (!nameOk(form.name.trim())) return notify(t('plates.invalid_name'), 'error')
      const diameter = Number(form.diameter)
      if (!decimalOk(form.diameter, 2) || diameter < 0.1 || !decimalOk(form.z, 0.5))
        return notify(t('plates.invalid_nozzle'), 'error')
      await saveNozzle(form.id, form.name.trim(), diameter, Number(form.z))
      setForm(empty)
      setAdding(false)
      notify(t('plates.nozzle_saved'))
    })
  const remove = (n: Nozzle) =>
    act(async () => {
      if (!(await ask(tpl('plates.nozzle_delete_confirm', { name: n.name }), true))) return
      await deleteNozzle(n.id)
      if (form.id === n.id) setForm(empty)
      notify(t('plates.nozzle_deleted'))
    })
  // Delete every nozzle with one confirmation; one request per nozzle, so each keeps the checks of a single delete.
  const removeAll = () =>
    act(async () => {
      if (!(await ask(tpl('plates.nozzle_delete_all_confirm', { n: lib.nozzles.length }), true))) return
      for (const n of [...lib.nozzles]) await deleteNozzle(n.id)
      setForm(empty)
      setAdding(false)
      notify(t('plates.nozzle_deleted_all'))
    })
  const [adding, setAdding] = useState(false)
  const formOpen = adding || Boolean(form.id)
  return (
    // Inside the library card, below "In the printer now": the nozzle adds its Z correction to the mounted plate.
    <div class="mt-4" data-plates-nozzles>
      <div class="mb-2 flex flex-wrap items-center gap-2">
        <h3 class="text-sm font-semibold">{t('plates.nozzles_title')}</h3>
        <InfoHint text={t('plates.nozzles_hint')} data-hint="nozzles" />
        <Button
          class="ml-auto min-h-8 px-3 text-xs"
          onClick={() => {
            if (formOpen) {
              setAdding(false)
              setForm(empty)
            } else setAdding(true)
          }}
          data-nozzle-add
        >
          {t(formOpen ? 'common.cancel' : 'plates.add_nozzle_open')}
        </Button>
        {lib.nozzles.length > 1 && (
          <More>
            <Button
              variant="danger"
              class="min-h-8 px-2 text-xs"
              disabled={locked}
              onClick={removeAll}
              data-nozzle-delete-all
            >
              {t('plates.nozzle_delete_all')}
            </Button>
          </More>
        )}
      </div>
      <div class="grid max-h-[9.5rem] gap-1.5 overflow-y-auto pr-1 [scrollbar-width:thin]" data-nozzle-list>
        {lib.nozzles.map(n => {
          const installed = lib.nozzle === n.id
          return (
            <div
              key={n.id}
              data-nozzle={n.id}
              class="flex flex-wrap items-center gap-x-3 gap-y-1.5 rounded-md border border-edge px-2.5 py-1.5 text-xs"
            >
              <b class="text-sm text-fg [overflow-wrap:anywhere]">{n.name}</b>
              <span class="text-muted">⌀ {n.diameter.toFixed(2)} mm</span>
              <span class="text-muted">
                {t('plates.nozzle_z')}: <b class="text-fg">{zText(n.z_offset)} mm</b>
              </span>
              {installed && <Tag tone="ok">{t('plates.nozzle_installed')}</Tag>}
              <span class="ml-auto flex items-center gap-1.5">
                <Button
                  class="min-h-7 px-2 text-xs"
                  disabled={locked}
                  onClick={() => act(() => selectNozzle(installed ? '' : n.id))}
                >
                  {t(installed ? 'plates.nozzle_remove_mark' : 'plates.nozzle_install')}
                </Button>
                <More>
                  <Button class="min-h-8 px-2 text-xs" disabled={locked} onClick={() => edit(n)}>
                    {t('plates.edit')}
                  </Button>
                  <Button variant="danger" class="min-h-8 px-2 text-xs" disabled={locked} onClick={() => remove(n)}>
                    {t('common.delete')}
                  </Button>
                </More>
              </span>
            </div>
          )
        })}
        {!lib.nozzles.length && <Notice>{t('plates.no_nozzles')}</Notice>}
      </div>
      <MoreBelow count={lib.nozzles.length} shown={3} />
      {formOpen && (
        <div class="mt-3 grid gap-2 cc2-sm:grid-cols-[1fr_7rem_8rem_auto]" data-nozzle-form>
          <label class="grid gap-1 text-xs">
            {t('common.name')}
            <Input
              value={form.name}
              maxLength={64}
              placeholder={t('plates.nozzle_name_placeholder')}
              onInput={e => setForm({ ...form, name: e.currentTarget.value })}
            />
          </label>
          <label class="grid gap-1 text-xs">
            {t('plates.nozzle_diameter_mm')}
            <Input
              type="number"
              step="0.05"
              min="0.1"
              max="2"
              value={form.diameter}
              onInput={e => setForm({ ...form, diameter: e.currentTarget.value })}
            />
          </label>
          <label class="grid gap-1 text-xs">
            {t('plates.nozzle_z_mm')}
            <Input
              type="number"
              step="0.005"
              min="-0.5"
              max="0.5"
              value={form.z}
              onInput={e => setForm({ ...form, z: e.currentTarget.value })}
            />
          </label>
          <div class="flex items-end gap-2">
            <Button variant="primary" disabled={locked || !form.name.trim()} onClick={save}>
              {t(form.id ? 'plates.save' : 'plates.add_nozzle')}
            </Button>
          </div>
        </div>
      )}
    </div>
  )
}

const SaveForm = ({
  lib,
  idle,
  busy,
  act,
  onSaved,
}: {
  lib: PlateLibrary
  idle: boolean
  busy: boolean
  act: Act
  onSaved?: () => void
}) => {
  const [side, setSide] = useState<'A' | 'B'>('A')
  const [name, setName] = useState('')
  const [z, setZ] = useState('0.000')
  const [temp, setTemp] = useState('60')
  const [nozzle, setNozzle] = useState<string | null>(null)
  const [capture, setCapture] = useState(false)
  const missing = lib.slots[side] !== 'mesh'
  const withMesh = capture && !missing
  const chosen = nozzle ?? lib.nozzle
  const save = () =>
    act(async () => {
      if (!nameOk(name.trim())) return notify(t('plates.invalid_name'), 'error')
      if (!zOk(z)) return notify(t('plates.invalid_z'), 'error')
      if (withMesh && !tempOk(temp)) return notify(t('common.invalid_bed_temperature'), 'error')
      if (withMesh) await savePlate(side, name.trim(), Number(z), Number(temp), chosen)
      else await createPlate(side, name.trim(), Number(z))
      setName('')
      setZ('0.000')
      notify(t(withMesh ? 'plates.saved_toast' : 'plates.created_without_mesh'))
      onSaved?.()
    })
  return (
    <Card>
      <CardHead icon="folder" title="plates.save_new" />
      <p class="mb-3 text-xs text-muted">{t('plates.save_hint')}</p>
      <label class="mb-3 flex items-center gap-2 text-xs">
        <input
          type="checkbox"
          checked={withMesh}
          disabled={missing || busy}
          onChange={e => setCapture(e.currentTarget.checked)}
        />
        {t('plates.include_existing_mesh')}
      </label>
      <div class="grid gap-2 cc2-sm:grid-cols-[7rem_1fr_8rem] cc2-lg:grid-cols-[7rem_1fr_8rem_7rem_10rem_auto]">
        <label class="grid gap-1 text-xs">
          {t('plates.side')}
          <Select value={side} onChange={e => setSide(e.currentTarget.value as 'A' | 'B')}>
            <option value="A">{t('print.side_a')}</option>
            <option value="B">{t('print.side_b')}</option>
          </Select>
        </label>
        <label class="grid gap-1 text-xs">
          {t('common.name')}
          <Input
            value={name}
            maxLength={64}
            placeholder={t('plates.name_placeholder')}
            onInput={e => setName(e.currentTarget.value)}
          />
        </label>
        <label class="grid gap-1 text-xs">
          {t('plates.z_offset_mm')}
          <Input type="number" step="0.005" min="-1" max="1" value={z} onInput={e => setZ(e.currentTarget.value)} />
        </label>
        <label class="grid gap-1 text-xs">
          {t('common.bed_temperature_c')}
          <Input
            type="number"
            min="30"
            max="110"
            step="1"
            value={temp}
            disabled={!withMesh}
            onInput={e => setTemp(e.currentTarget.value)}
          />
        </label>
        <label class="grid gap-1 text-xs">
          {t('common.nozzle')}
          <NozzleSelect lib={lib} value={chosen} disabled={!withMesh} onChange={setNozzle} />
        </label>
        <div class="flex items-end">
          <Button
            variant="primary"
            wide
            disabled={busy || !idle || !lib.available || Boolean(lib.pending) || !name.trim()}
            title={idle ? undefined : t('common.available_when_idle')}
            onClick={save}
          >
            {t('plates.save_plate')}
          </Button>
        </div>
      </div>
      {!withMesh && <Notice>{t('plates.empty_profile_hint')}</Notice>}
    </Card>
  )
}

// Selection is inert; the explicit action mounts and verifies the chosen measure.
const MeshSelector = ({ lib, idle, busy, act }: { lib: PlateLibrary; idle: boolean; busy: boolean; act: Act }) => {
  const [choice, setChoice] = useState('')
  const chosen = findMeasure(lib, choice)
  const active = lib.mesh_profile?.startsWith('cc2_') ? findMeasure(lib, lib.mesh_profile.slice(4)) : null
  const activeLabel = active
    ? `${active.plate.name} · ${active.measure.temp} °C`
    : lib.mesh_profile === 'default'
      ? t('print.side_a')
      : lib.mesh_profile === 'default1'
        ? t('print.side_b')
        : lib.mesh_profile || t('common.not_set')
  const locked = busy || !idle || !lib.available || Boolean(lib.pending) || lib.calibration.stage !== 'off'
  const apply = async () => {
    if (locked || !chosen) return
    const { plate, measure } = chosen
    if (!(await ask(tpl('plates.mesh_confirm', { name: plate.name, side: plate.side, temp: measure.temp })))) return
    await mount(plate, measure)
  }
  return (
    <div class="mt-3 rounded-md border border-edge p-3" data-mesh-selector>
      <div class="flex flex-col gap-3 cc2-sm:flex-row cc2-sm:items-end">
        <label class="grid min-w-0 flex-1 gap-1 text-xs">
          {t('plates.mesh_selector')}
          <Select
            aria-label={t('plates.mesh_selector')}
            value={chosen ? choice : ''}
            disabled={busy}
            onChange={e => setChoice(e.currentTarget.value)}
          >
            <option value="">{t('plates.mesh_choose')}</option>
            {lib.plates.map(p => (
              <optgroup key={p.id} label={`${p.name} · ${tpl('plates.side_n', { side: p.side })}`}>
                {p.measures.map(m => (
                  <option key={m.id} value={m.id}>
                    {p.name} · {tpl('plates.side_n', { side: p.side })} · {m.temp} °C · {savedOn(m.measured)}
                  </option>
                ))}
              </optgroup>
            ))}
          </Select>
        </label>
        <Button
          variant="primary"
          disabled={locked || !chosen}
          title={!idle ? t('common.available_when_idle') : undefined}
          onClick={() => act(apply)}
        >
          {t('plates.mesh_apply')}
        </Button>
      </div>
      <p class="mt-2 text-xs">
        {t('plates.mesh_active')}: <b>{activeLabel}</b>
      </p>
      <p class="mt-2 text-xs text-muted">{t('plates.mesh_scope')}</p>
    </div>
  )
}

// What happened to the measurement chosen for the latest print started here.
const PrintMeshNotice = ({ lib }: { lib: PlateLibrary }) => {
  const { state, measure, result } = lib.print_mesh
  const blendTemp = lib.match?.blend_temp || 0
  if (blendTemp && !measure) {
    if (state !== 'off') return <Notice icon="bolt">{tpl('plates.print_mesh_waiting', { temp: blendTemp })}</Notice>
    if (result === 'loaded') return <Notice>{tpl('plates.print_mesh_blend_loaded', { temp: blendTemp })}</Notice>
    if (result === 'missed') return <Warn>{tpl('plates.print_mesh_missed', { temp: blendTemp })}</Warn>
    if (result === 'failed') return <Warn>{tpl('plates.print_mesh_failed', { temp: blendTemp })}</Warn>
    return null
  }
  const found = findMeasure(lib, measure)
  if (!found) return null
  const temp = found.measure.temp
  if (state !== 'off') return <Notice icon="bolt">{tpl('plates.print_mesh_waiting', { temp })}</Notice>
  if (result === 'loaded')
    return (
      <Notice>
        {tpl(lib.print_mesh.source === 'auto' ? 'plates.print_mesh_auto_loaded' : 'plates.print_mesh_loaded', { temp })}
      </Notice>
    )
  if (result === 'missed') return <Warn>{tpl('plates.print_mesh_missed', { temp })}</Warn>
  if (result === 'failed') return <Warn>{tpl('plates.print_mesh_failed', { temp })}</Warn>
  return null
}

// Match the mesh to the bed temperature of prints sent from a slicer or the touchscreen (#296).
const MeshMatchSwitch = ({ lib, busy, act }: { lib: PlateLibrary; busy: boolean; act: Act }) => {
  const on = Boolean(lib.match?.enabled)
  return (
    <div class="mt-3">
      <div class="flex items-center gap-2">
        <Button
          wide
          class="h-auto min-h-9 min-w-0 flex-1 justify-between py-1 text-left whitespace-normal"
          data-mesh-match
          aria-pressed={on}
          disabled={busy || !lib.available || !lib.match}
          onClick={() => act(() => setMeshMatch(!on))}
        >
          {t('plates.match_temperature')}
          <small class={on ? 'text-cyan' : 'text-muted'}>{t(on ? 'plates.match_on' : 'plates.match_off')}</small>
        </Button>
        <InfoHint text={t('plates.match_note')} align="right" data-hint="match" />
      </div>
      {on && <BlendStatus lib={lib} busy={busy} act={act} />}
    </div>
  )
}

// The blended meshes written for the mounted plate and whether the printer has loaded them.
const BlendStatus = ({ lib, busy, act }: { lib: PlateLibrary; busy: boolean; act: Act }) => {
  const blends = lib.match?.blends
  if (!blends || blends.state === '' || blends.state === 'off') return null
  const temps = blends.temps.map(b => b.temp).join(', ')
  if (blends.state === 'too_few') return <p class="mt-1.5 text-xs text-muted">{t('plates.blends_too_few')}</p>
  if (blends.state === 'write_failed') return <Warn>{t('plates.blends_write_failed')}</Warn>
  if (blends.state === 'planned')
    return <p class="mt-1.5 text-xs text-muted">{tpl('plates.blends_planned', { temps })}</p>
  if (blends.state === 'ready')
    return (
      <p class="mt-1.5 text-xs" data-blends="ready">
        {tpl('plates.blends_ready', { temps })}
      </p>
    )
  const restart = async () => {
    if (!(await ask(t('plates.blends_restart_confirm')))) return
    await restartForBlends()
    notify(t('plates.blends_restart'))
  }
  return (
    <Notice icon="bolt">
      <span data-blends="written">
        {tpl('plates.blends_written', { temps })}{' '}
        <Button variant="ghost" class="text-xs" disabled={busy} onClick={() => act(restart)}>
          {t('plates.blends_restart')}
        </Button>
      </span>
    </Notice>
  )
}

export const Plates = () => {
  const { data: lib, ok, rebooting } = plates.use()
  const v = view(printer.use().data)
  const { busy, act } = useAct()
  usePoll(refreshPlates, rebooting || lib?.pending || (lib && lib.print_mesh.state !== 'off') ? 4000 : 20000)
  const current = lib?.pending ? undefined : lib?.plates.find(p => p.id === lib.current)
  const waiting = lib?.plates.find(p => p.id === lib.pending)
  const installed = lib?.nozzles.find(n => n.id === lib.nozzle)
  const [adding, setAdding] = useState(false)
  return (
    <div class="grid gap-3.5">
      <Card>
        <CardHead icon="layers" title="plates.title" sub="plates.subtitle" />
        {!lib ? (
          <Notice>{t(ok ? 'plates.loading' : 'plates.offline')}</Notice>
        ) : (
          <>
            {!lib.available && <Warn>{tpl('plates.unavailable', { error: lib.error })}</Warn>}
            {lib.pending_conflict ? (
              <Warn>{t('plates.pending_save_conflict')}</Warn>
            ) : rebooting || waiting ? (
              <Notice icon="bolt">{tpl('plates.restarting', { name: waiting?.name || current?.name || '' })}</Notice>
            ) : lib.result === 'verify_failed' ? (
              <Warn>{t('plates.verify_failed')}</Warn>
            ) : lib.result === 'reboot_failed' ? (
              <Warn>{t('plates.reboot_failed')}</Warn>
            ) : null}
            <div class="grid grid-cols-1 items-start gap-3.5 cc2-lg:grid-cols-2" data-plates-top>
              <div class="min-w-0">
                <h3 class="mt-1 mb-2 text-sm font-semibold" data-plates-now>
                  {t('plates.now_title')}
                </h3>
                <div class="flex flex-wrap items-center gap-x-3 gap-y-2 rounded-md border border-edge bg-field/60 px-3 py-2.5">
                  {current ? (
                    <>
                      <span class="text-xs text-muted">{t('plates.mounted_plate')}</span>
                      <b class="[overflow-wrap:anywhere]">{current.name}</b>
                      <Tag>{tpl('plates.side_n', { side: current.side })}</Tag>
                      <span class="text-xs">
                        {t('plates.z_offset')}: <b>{zText(current.z_offset)} mm</b>
                        {installed && lib.z_effective !== null && (
                          <>
                            {' '}
                            {tpl('plates.z_with_nozzle', {
                              name: installed.name,
                              delta: zText(installed.z_offset),
                              z: zText(lib.z_effective),
                            })}
                          </>
                        )}
                      </span>
                      <small class="text-muted">{t(lib.z_applied ? 'plates.z_applied' : 'plates.z_waiting')}</small>
                      {(lib.z_effective ?? current.z_offset) !== 0 && (
                        <InfoHint
                          text={tpl('plates.screen_z_note', {
                            z: zText(lib.z_effective ?? current.z_offset),
                            live: t('common.live_z_offset'),
                            page: t('common.control'),
                            edit: t('plates.edit'),
                          })}
                          data-hint="screen-z"
                        />
                      )}
                      <Button
                        variant="ghost"
                        class="ml-auto text-xs"
                        disabled={busy || Boolean(lib.pending)}
                        title={t('plates.forget_mounted_hint')}
                        onClick={() => act(unmountPlate)}
                      >
                        {t('plates.forget_mounted')}
                      </Button>
                    </>
                  ) : (
                    <span class="text-xs text-muted">{t('plates.no_plate_mounted')}</span>
                  )}
                </div>
                <NozzlesCard lib={lib} busy={busy} act={act} />
              </div>
              <div class="min-w-0 [&>[data-mesh-selector]]:mt-0 cc2-lg:mt-7">
                <MeshSelector lib={lib} idle={v.idle && ok} busy={busy || rebooting} act={act} />
                <MeshMatchSwitch lib={lib} busy={busy} act={act} />
              </div>
            </div>
            <PrintMeshNotice lib={lib} />
            <div class="mt-5 mb-2 flex items-center gap-2" data-plates-mine>
              <h3 class="mr-auto text-sm font-semibold">{t('plates.mine_title')}</h3>
              <Button class="min-h-8 px-3 text-xs" onClick={() => setAdding(a => !a)} data-plates-add>
                {t(adding ? 'common.cancel' : 'plates.add_plate')}
              </Button>
            </div>
            {adding && (
              <SaveForm lib={lib} idle={v.idle && ok} busy={busy} act={act} onSaved={() => setAdding(false)} />
            )}
            <div class="mt-3 grid auto-rows-fr gap-2.5 cc2-lg:grid-cols-2" data-plate-grid>
              {groupSides(lib.plates).map(p => (
                <PlateCard key={p.id} p={p} lib={lib} idle={v.idle && ok} busy={busy} act={act} />
              ))}
            </div>
            {!lib.plates.length && <Notice>{t('plates.empty')}</Notice>}
          </>
        )}
        <details class="group mt-3 rounded-md border border-edge bg-field/40 text-xs" data-plates-how>
          <summary class="flex cursor-pointer items-center gap-2 px-3 py-2 text-muted select-none [&::-webkit-details-marker]:hidden">
            <span class="text-cyan transition-transform group-open:rotate-90">›</span>
            {t('plates.how_title')}
          </summary>
          <p class="border-t border-edge px-3 py-2 text-muted">
            {t('plates.how_it_works')}
            <br />
            {t('plates.temperatures_note')}
            <br />
            {t('plates.adaptive_note')}
          </p>
        </details>
      </Card>
    </div>
  )
}
