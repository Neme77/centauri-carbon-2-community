import { useRef, useState } from 'preact/hooks'
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
  selectNozzle,
  tempOk,
  unmountPlate,
  zText,
} from '@/lib/plates'
import { openPage, printer, view, zoffset } from '@/lib/state'

const savedOn = (s: number) => (s > 0 ? new Date(s * 1000).toLocaleDateString() : '—')
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
            min="40"
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
      <span class="ml-auto flex flex-wrap gap-1.5">
        <Button
          class="min-h-7 px-2 text-xs"
          onClick={() => {
            meshProfile.set({ profile: `measure:${m.id}` })
            openPage('bed')
          }}
        >
          {t('plates.view_mesh')}
        </Button>
        <Button
          class="min-h-7 px-2 text-xs"
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
            class="min-h-7 px-2 text-xs"
            disabled={locked || !idle}
            title={idleOnly}
            onClick={() => act(() => mount(p, m))}
          >
            {t('plates.write_to_slot')}
          </Button>
        )}
        {p.measures.length > 1 && (
          <Button variant="danger" class="min-h-7 px-2 text-xs" disabled={locked} onClick={remove}>
            {t('common.delete')}
          </Button>
        )}
      </span>
    </div>
  )
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
  const mounted = lib.current === p.id
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
  const remove = () =>
    act(async () => {
      if (!(await ask(tpl('plates.delete_confirm', { name: p.name }), true))) return
      await deletePlate(p.id)
      notify(t('plates.deleted_toast'))
    })
  return (
    <div class="rounded-lg border border-edge bg-field/40 p-3" data-plate={p.id}>
      <div class="flex flex-wrap items-center gap-2">
        <strong class="mr-auto min-w-0 text-sm [overflow-wrap:anywhere]">{p.name}</strong>
        <Tag>{tpl('plates.side_n', { side: p.side })}</Tag>
        {mounted && <Tag tone="ok">{t('plates.mounted')}</Tag>}
        {p.in_printer && <Tag>{t('plates.in_printer')}</Tag>}
      </div>
      <div class="mt-2 text-xs text-muted">
        {t('plates.z_offset')}: <b class="text-fg">{zText(p.z_offset)} mm</b>
      </div>
      <fieldset class="mt-2 grid min-w-0 gap-1.5" aria-label={t('plates.measures')}>
        {p.measures.map(m => (
          <MeasureRow key={m.id} p={p} m={m} lib={lib} idle={idle} locked={locked} act={act} />
        ))}
      </fieldset>
      {mounted && !p.in_printer && !lib.pending && (
        <Warn>
          {tpl('plates.mesh_differs', { side: p.side })}
          <div class="mt-2 flex flex-wrap items-end gap-2">
            <label class="grid gap-1 text-xs">
              {t('common.bed_temperature_c')}
              <Input
                class="w-24"
                type="number"
                min="40"
                max="110"
                step="1"
                value={temp}
                onInput={e => setTemp(e.currentTarget.value)}
              />
            </label>
            <Button
              class="text-xs"
              disabled={locked || !idle}
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
        <div class="mt-3 flex flex-wrap gap-2">
          {!mounted && (
            <Button
              variant="primary"
              class="text-xs"
              disabled={locked || !idle}
              title={idleOnly}
              onClick={() => act(() => mount(p))}
            >
              {t('plates.mount')}
            </Button>
          )}
          <Button
            class="text-xs"
            disabled={locked}
            onClick={() => {
              setName(p.name)
              setZ(p.z_offset.toFixed(3))
              setEditing(true)
            }}
          >
            {t('plates.edit')}
          </Button>
          <Button variant="danger" class="text-xs" disabled={locked} onClick={remove}>
            {t('common.delete')}
          </Button>
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
      notify(t('plates.nozzle_saved'))
    })
  const remove = (n: Nozzle) =>
    act(async () => {
      if (!(await ask(tpl('plates.nozzle_delete_confirm', { name: n.name }), true))) return
      await deleteNozzle(n.id)
      if (form.id === n.id) setForm(empty)
      notify(t('plates.nozzle_deleted'))
    })
  return (
    <Card>
      <CardHead icon="target" title="plates.nozzles_title" />
      <p class="mb-3 text-xs text-muted">{t('plates.nozzles_hint')}</p>
      <div class="grid gap-1.5">
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
              <span class="ml-auto flex flex-wrap gap-1.5">
                <Button
                  class="min-h-7 px-2 text-xs"
                  disabled={locked}
                  onClick={() => act(() => selectNozzle(installed ? '' : n.id))}
                >
                  {t(installed ? 'plates.nozzle_remove_mark' : 'plates.nozzle_install')}
                </Button>
                <Button class="min-h-7 px-2 text-xs" disabled={locked} onClick={() => edit(n)}>
                  {t('plates.edit')}
                </Button>
                <Button variant="danger" class="min-h-7 px-2 text-xs" disabled={locked} onClick={() => remove(n)}>
                  {t('common.delete')}
                </Button>
              </span>
            </div>
          )
        })}
        {!lib.nozzles.length && <Notice>{t('plates.no_nozzles')}</Notice>}
      </div>
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
          {form.id && (
            <Button disabled={busy} onClick={() => setForm(empty)}>
              {t('common.cancel')}
            </Button>
          )}
        </div>
      </div>
    </Card>
  )
}

const SaveForm = ({ lib, idle, busy, act }: { lib: PlateLibrary; idle: boolean; busy: boolean; act: Act }) => {
  const [side, setSide] = useState<'A' | 'B'>('A')
  const [name, setName] = useState('')
  const [z, setZ] = useState('0.000')
  const [temp, setTemp] = useState('60')
  const [nozzle, setNozzle] = useState<string | null>(null)
  const missing = lib.slots[side] !== 'mesh'
  const chosen = nozzle ?? lib.nozzle
  const save = () =>
    act(async () => {
      if (!nameOk(name.trim())) return notify(t('plates.invalid_name'), 'error')
      if (!zOk(z)) return notify(t('plates.invalid_z'), 'error')
      if (!tempOk(temp)) return notify(t('common.invalid_bed_temperature'), 'error')
      await savePlate(side, name.trim(), Number(z), Number(temp), chosen)
      setName('')
      setZ('0.000')
      notify(t('plates.saved_toast'))
    })
  return (
    <Card>
      <CardHead icon="folder" title="plates.save_new" />
      <p class="mb-3 text-xs text-muted">{t('plates.save_hint')}</p>
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
          <Input type="number" min="40" max="110" step="1" value={temp} onInput={e => setTemp(e.currentTarget.value)} />
        </label>
        <label class="grid gap-1 text-xs">
          {t('common.nozzle')}
          <NozzleSelect lib={lib} value={chosen} onChange={setNozzle} />
        </label>
        <div class="flex items-end">
          <Button
            variant="primary"
            wide
            disabled={busy || !idle || !lib.available || Boolean(lib.pending) || missing || !name.trim()}
            title={idle ? undefined : t('common.available_when_idle')}
            onClick={save}
          >
            {t('plates.save_plate')}
          </Button>
        </div>
      </div>
      {missing && <Notice>{t('print.this_build_plate_side_has_no')}</Notice>}
    </Card>
  )
}

// What happened to the measurement chosen for the latest print started here.
const PrintMeshNotice = ({ lib }: { lib: PlateLibrary }) => {
  const { state, measure, result } = lib.print_mesh
  const found = findMeasure(lib, measure)
  if (!found) return null
  const temp = found.measure.temp
  if (state !== 'off') return <Notice icon="bolt">{tpl('plates.print_mesh_waiting', { temp })}</Notice>
  if (result === 'loaded') return <Notice>{tpl('plates.print_mesh_loaded', { temp })}</Notice>
  if (result === 'missed') return <Warn>{tpl('plates.print_mesh_missed', { temp })}</Warn>
  if (result === 'failed') return <Warn>{tpl('plates.print_mesh_failed', { temp })}</Warn>
  return null
}

export const Plates = () => {
  const { data: lib, ok, rebooting } = plates.use()
  const v = view(printer.use().data)
  const { busy, act } = useAct()
  usePoll(refreshPlates, rebooting || lib?.pending || (lib && lib.print_mesh.state !== 'off') ? 4000 : 20000)
  const current = lib?.plates.find(p => p.id === lib.current)
  const waiting = lib?.plates.find(p => p.id === lib.pending)
  const installed = lib?.nozzles.find(n => n.id === lib.nozzle)
  return (
    <div class="grid gap-3.5">
      <Card>
        <CardHead icon="layers" title="plates.title" sub="plates.subtitle" />
        {!lib ? (
          <Notice>{t(ok ? 'plates.loading' : 'plates.offline')}</Notice>
        ) : (
          <>
            {!lib.available && <Warn>{tpl('plates.unavailable', { error: lib.error })}</Warn>}
            {rebooting || waiting ? (
              <Notice icon="bolt">{tpl('plates.restarting', { name: waiting?.name || current?.name || '' })}</Notice>
            ) : lib.result === 'verify_failed' ? (
              <Warn>{t('plates.verify_failed')}</Warn>
            ) : lib.result === 'reboot_failed' ? (
              <Warn>{t('plates.reboot_failed')}</Warn>
            ) : null}
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
                  <Button
                    variant="ghost"
                    class="ml-auto text-xs"
                    disabled={busy || Boolean(lib.pending)}
                    onClick={() => act(unmountPlate)}
                  >
                    {t('plates.forget_mounted')}
                  </Button>
                </>
              ) : (
                <span class="text-xs text-muted">{t('plates.no_plate_mounted')}</span>
              )}
            </div>
            {current && (lib.z_effective ?? current.z_offset) !== 0 && (
              <Notice>
                {tpl('plates.screen_z_note', {
                  z: zText(lib.z_effective ?? current.z_offset),
                  live: t('common.live_z_offset'),
                  page: t('common.control'),
                  edit: t('plates.edit'),
                })}
              </Notice>
            )}
            <PrintMeshNotice lib={lib} />
            <div class="mt-3 grid gap-2.5 cc2-lg:grid-cols-2">
              {lib.plates.map(p => (
                <PlateCard key={p.id} p={p} lib={lib} idle={v.idle && ok} busy={busy} act={act} />
              ))}
            </div>
            {!lib.plates.length && <Notice>{t('plates.empty')}</Notice>}
          </>
        )}
        <Notice>
          <span>
            {t('plates.how_it_works')}
            <br />
            {t('plates.temperatures_note')}
            <br />
            {t('plates.adaptive_note')}
          </span>
        </Notice>
      </Card>
      {lib && <NozzlesCard lib={lib} busy={busy} act={act} />}
      {lib && <SaveForm lib={lib} idle={v.idle && ok} busy={busy} act={act} />}
    </div>
  )
}
