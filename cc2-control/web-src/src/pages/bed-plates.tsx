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
  deletePlate,
  editPlate,
  meshProfile,
  meshRange,
  mountPlate,
  type Plate,
  type PlateLibrary,
  plates,
  recapturePlate,
  refreshPlates,
  savePlate,
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
const zOk = (text: string) => /^[+-]?\d*\.?\d+$/.test(text.trim()) && Math.abs(Number(text)) <= 1

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

// The printer must restart when the plate's mesh is not in its slot: confirm, then send the REBOOT line.
async function mount(p: Plate) {
  try {
    await mountPlate(p.id)
    notify(tpl('plates.mounted_toast', { name: p.name }))
  } catch (e: any) {
    if (!e?.data?.reboot_required) throw e
    if (!(await ask(tpl('plates.reboot_confirm', { name: p.name, side: p.side }), true))) return
    await mountPlate(p.id, true)
    plates.set({ rebooting: true })
    notify(t('plates.restart_requested'))
  }
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
      <div class="mt-2 grid gap-x-3 gap-y-1 text-xs text-muted cc2-sm:grid-cols-3">
        <span>
          {t('plates.z_offset')}: <b class="text-fg">{zText(p.z_offset)} mm</b>
        </span>
        <span>
          {t('plates.mesh_range')}: <b class="text-fg">{meshRange(p.mesh).toFixed(3)} mm</b>
        </span>
        <span>{tpl('plates.saved_on', { date: savedOn(p.measured) })}</span>
      </div>
      {mounted && !p.in_printer && !lib.pending && (
        <Warn>
          {tpl('plates.mesh_differs', { side: p.side })}
          <div class="mt-2 flex flex-wrap gap-2">
            <Button
              class="text-xs"
              disabled={locked || !idle}
              title={idleOnly}
              onClick={() =>
                act(async () => {
                  await recapturePlate(p.id)
                  notify(t('plates.updated_toast'))
                })
              }
            >
              {t('plates.update_from_printer')}
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
            onClick={() => {
              meshProfile.set({ profile: `plate:${p.id}` })
              openPage('bed')
            }}
          >
            {t('plates.view_mesh')}
          </Button>
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

const SaveForm = ({ lib, idle, busy, act }: { lib: PlateLibrary; idle: boolean; busy: boolean; act: Act }) => {
  const [side, setSide] = useState<'A' | 'B'>('A')
  const [name, setName] = useState('')
  const [z, setZ] = useState('0.000')
  const missing = lib.slots[side] !== 'mesh'
  const save = () =>
    act(async () => {
      if (!nameOk(name.trim())) return notify(t('plates.invalid_name'), 'error')
      if (!zOk(z)) return notify(t('plates.invalid_z'), 'error')
      await savePlate(side, name.trim(), Number(z))
      setName('')
      setZ('0.000')
      notify(t('plates.saved_toast'))
    })
  return (
    <Card>
      <CardHead icon="folder" title="plates.save_new" />
      <p class="mb-3 text-xs text-muted">{t('plates.save_hint')}</p>
      <div class="grid gap-2 cc2-sm:grid-cols-[8rem_1fr_9rem_auto]">
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

export const Plates = () => {
  const { data: lib, ok, rebooting } = plates.use()
  const v = view(printer.use().data)
  const { busy, act } = useAct()
  usePoll(refreshPlates, rebooting || lib?.pending ? 4000 : 20000)
  const current = lib?.plates.find(p => p.id === lib.current)
  const waiting = lib?.plates.find(p => p.id === lib.pending)
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
            {current && current.z_offset !== 0 && (
              <Notice>
                {tpl('plates.screen_z_note', {
                  z: zText(current.z_offset),
                  live: t('common.live_z_offset'),
                  page: t('common.control'),
                  edit: t('plates.edit'),
                })}
              </Notice>
            )}
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
            {t('plates.adaptive_note')}
          </span>
        </Notice>
      </Card>
      {lib && <SaveForm lib={lib} idle={v.idle && ok} busy={busy} act={act} />}
    </div>
  )
}
