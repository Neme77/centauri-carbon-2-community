import { useEffect, useRef, useState } from 'preact/hooks'
import { ArrowBigDown, ArrowBigLeft, ArrowBigRight, ArrowBigUp } from 'lucide-preact'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Dot, Pip, Tag } from '@/components/ui/badge'
import { Input, Select } from '@/components/ui/field'
import { Icon } from '@/components/icons'
import { FanSlider, Notice, Warn } from '@/components/shared'
import { control, errText, notify, post, request } from '@/lib/api'
import { PrintTuning } from '@/components/print-tuning'
import { ask } from '@/lib/confirm'
import { t, tpl } from '@/lib/i18n'
import { store } from '@/lib/store'
import { stateText } from '@/lib/machine'
import { num } from '@/lib/format'
import { usePoll } from '@/lib/poll'
import { presets, printer, refreshPrinter, savePresets, view, zoffset } from '@/lib/state'

const STEPS = [0.1, 1, 10, 30, 50]
const I = { size: 20, strokeWidth: 1 }
const B = { size: 22, strokeWidth: 1 }

const Movement = ({ v }: { v: ReturnType<typeof view> }) => {
  const [step, setStep] = useState(0.1)
  const can = (axis: string) => v.idle && v.homed.includes(axis)
  const move = (axis: string, dir: number) => control(`move:${axis}:${dir * step}`)
  // The printer only reports "Manual homing" on its next poll: lock the buttons meanwhile and pull the state early.
  const [homing, setHoming] = useState(false)
  const home = async (axis: string) => {
    if (homing) return
    setHoming(true)
    if (await control(`home:${axis}`)) {
      setTimeout(refreshPrinter, 400)
      setTimeout(refreshPrinter, 1200)
    }
    setTimeout(() => setHoming(false), 1500)
  }
  const pad = 'min-h-11 text-xl'
  return (
    <Card class="cc2-movement">
      <CardHead icon="control" title="control.movement" />
      <Button wide class="min-h-12" disabled={!v.idle || homing} onClick={() => home('ALL')}>
        <Icon n="home" />
        {t('common.home_all')}
      </Button>
      <div class="mt-3 grid grid-cols-3 gap-2.5">
        {['X', 'Y', 'Z'].map(a => (
          <Button key={a} class="min-h-16 flex-col gap-1" disabled={!v.idle || homing} onClick={() => home(a)}>
            <Icon n="home" class="text-cyan" />
            {t(a === 'X' ? 'common.home_x' : a === 'Y' ? 'common.home_y' : 'common.home_z')}
          </Button>
        ))}
      </div>
      <div class="cc2-motion-pad my-4 grid grid-cols-[1.4fr_.65fr] gap-5 border-t border-edge pt-4">
        <div>
          <div class="mb-2 text-[13px]">{t('control.xy_move')}</div>
          <div class="grid grid-cols-3 gap-1.5">
            <span />
            <Button
              class={pad}
              aria-label={t('control.move_y_positive')}
              disabled={!can('y')}
              onClick={() => move('Y', 1)}
            >
              <ArrowBigUp {...B} />
            </Button>
            <span />
            <Button
              class={pad}
              aria-label={t('control.move_x_negative')}
              disabled={!can('x')}
              onClick={() => move('X', -1)}
            >
              <ArrowBigLeft {...B} />
            </Button>
            <span />
            <Button
              class={pad}
              aria-label={t('control.move_x_positive')}
              disabled={!can('x')}
              onClick={() => move('X', 1)}
            >
              <ArrowBigRight {...B} />
            </Button>
            <span />
            <Button
              class={pad}
              aria-label={t('control.move_y_negative')}
              disabled={!can('y')}
              onClick={() => move('Y', -1)}
            >
              <ArrowBigDown {...B} />
            </Button>
          </div>
        </div>
        <div class="border-l border-edge pl-5">
          <div class="mb-2 text-[13px]">{t('control.z_move')}</div>
          <div class="grid gap-1.5">
            <Button
              class={pad}
              aria-label={t('control.move_z_positive')}
              disabled={!can('z')}
              onClick={() => move('Z', 1)}
            >
              <ArrowBigUp {...B} />
            </Button>
            <Button
              class={pad}
              aria-label={t('control.move_z_negative')}
              disabled={!can('z')}
              onClick={() => move('Z', -1)}
            >
              <ArrowBigDown {...B} />
            </Button>
          </div>
        </div>
      </div>
      <small class="text-muted">{t('control.step_size')}</small>
      <div class="my-2 flex gap-2">
        {STEPS.map(s => (
          <Button
            key={s}
            variant={s === step ? 'active' : 'default'}
            class="min-w-0 flex-1 whitespace-nowrap px-1 text-xs"
            onClick={() => setStep(s)}
          >
            {s} mm
          </Button>
        ))}
      </div>
      {!(v.idle && ['x', 'y', 'z'].every(a => v.homed.includes(a))) && (
        <Notice>{t('control.motion_requires_homing_commands')}</Notice>
      )}
      <div class="mt-4 border-t border-edge pt-3">
        <small class="text-muted">{t('control.current_position')}</small>
        <div class="my-2 grid grid-cols-3">
          {(['x', 'y', 'z'] as const).map(a => (
            <div key={a}>
              <small class="text-muted">{a.toUpperCase()}</small>
              <strong class="mt-1 block text-[15px]">{v.pos(a)}</strong>
            </div>
          ))}
        </div>
        <small class="text-muted">{t('control.homed_status')}</small>
        <div class="mt-2 grid grid-cols-3">
          {(['x', 'y', 'z'] as const).map(a => {
            const ok = v.homed.includes(a)
            return (
              <small key={a} class={ok ? 'text-cyan' : 'text-muted'}>
                <Pip hollow={!ok} /> {a.toUpperCase()} {t(ok ? 'control.homed' : 'control.not_homed')}
              </small>
            )
          })}
        </div>
      </div>
    </Card>
  )
}

const Temperatures = ({ d, v }: { d: any; v: ReturnType<typeof view> }) => {
  const list = presets.use().list
  const canSet = Boolean(d?.connected) && (v.idle || v.printing || v.paused)
  const [nozzle, setNozzle] = useState('0'),
    [bed, setBed] = useState('0')
  const seeded = useRef(false)
  // Start from the printer's current targets, so Apply never silently sends 0/0 over a running preheat.
  useEffect(() => {
    if (
      seeded.current ||
      !d?.connected ||
      d.extruder?.target == null ||
      d.heater_bed?.target == null ||
      !Number.isFinite(Number(d.extruder?.target)) ||
      !Number.isFinite(Number(d.heater_bed?.target))
    )
      return
    seeded.current = true
    setNozzle(String(Math.round(Number(d.extruder?.target) || 0)))
    setBed(String(Math.round(Number(d.heater_bed?.target) || 0)))
  }, [d])
  const active = list.find(p => String(p.nozzle) === nozzle && String(p.bed) === bed)?.name
  const apply = async () => {
    if (!canSet || !seeded.current) return
    const n = Number(nozzle),
      b = Number(bed)
    if (
      !nozzle.trim() ||
      !bed.trim() ||
      !Number.isFinite(n) ||
      n < 0 ||
      n > 300 ||
      !Number.isFinite(b) ||
      b < 0 ||
      b > 120
    )
      return notify(t('control.invalid_temperature_target'), 'error')
    await control(`heaters:set:${n}:${b}`)
  }
  return (
    <Card>
      <CardHead icon="temp" title="common.temperatures" />
      {(
        [
          ['red', 'common.nozzle', d?.extruder, nozzle, setNozzle, 300],
          ['blue', 'common.heated_bed', d?.heater_bed, bed, setBed, 120],
        ] as const
      ).map(([dot, label, heater, val, set, max]) => (
        <div key={label} class="my-4 flex items-center gap-2.5">
          <Dot c={dot} />
          <span>{t(label)}</span>
          <strong class="ml-auto text-[15px]">
            {num(heater?.temperature)}
            {Number(heater?.target) > 0 ? ` / ${num(heater.target)}` : ''} °C
          </strong>
          <Input
            class="w-18 text-center"
            type="number"
            min="0"
            max={max}
            value={val}
            disabled={!canSet}
            aria-label={tpl('control.name_target', { name: t(label) })}
            onInput={e => set(e.currentTarget.value)}
            onKeyDown={e => e.key === 'Enter' && apply()}
          />
          <small>°C</small>
        </div>
      ))}
      <Button wide disabled={!canSet} onClick={apply}>
        {t('control.apply_targets')}
      </Button>
      {!canSet && <Notice>{t('common.available_when_idle')}</Notice>}
      <div class="mt-4 border-t border-edge pt-3">
        <small class="text-muted">{t('control.temperature_presets')}</small>
        <div class="mt-2 flex gap-2">
          {list.map(p => (
            <Button
              key={p.name}
              class="min-w-0 flex-1 truncate px-1 text-xs"
              variant={active === p.name ? 'active' : 'default'}
              disabled={!v.idle}
              title={`${p.nozzle} °C nozzle / ${p.bed} °C bed`}
              onClick={() => {
                setNozzle(String(p.nozzle))
                setBed(String(p.bed))
                notify(
                  tpl('control.name_targets_nozzle_bed_c_loaded', {
                    name: p.name,
                    nozzle: p.nozzle,
                    bed: p.bed,
                  })
                )
              }}
            >
              {p.name}
            </Button>
          ))}
        </div>
      </div>
    </Card>
  )
}

const InputShaper = ({ d, v }: { d: any; v: ReturnType<typeof view> }) => {
  const [status, setStatus] = useState<any>(null)
  const [sending, setSending] = useState(false)
  const lock = useRef(false)
  const refresh = async () => {
    try {
      setStatus(await request('/api/console'))
    } catch {
      setStatus(null)
    }
  }
  usePoll(refresh, 3000)
  const idle =
    printer.use().ok &&
    d?.connected &&
    d?.machine?.status === 1 &&
    d?.last_message_age >= 0 &&
    d?.last_message_age <= 15
  const homed = ['x', 'y', 'z'].every(a => v.homed.includes(a))
  const can = idle && homed && status && !status.busy && !sending
  const start = async () => {
    if (!can || lock.current) return
    lock.current = true
    setSending(true)
    try {
      if (!(await ask(t('control.shaper_confirm')))) return
      await post('/api/console/command', 'SHAPER_CALIBRATE')
      await refresh()
      notify(t('common.command_accepted'))
    } catch (e) {
      notify(errText(e), 'error')
    } finally {
      lock.current = false
      setSending(false)
    }
  }
  const own = status?.command === 'SHAPER_CALIBRATE'
  return (
    <Card class="cc2-shaper">
      <CardHead icon="settings" title="control.shaper_title" />
      <p class="mb-3 text-xs text-muted">{t('control.shaper_help')}</p>
      <Button wide disabled={!can} onClick={start}>
        {t('control.shaper_start')}
      </Button>
      {!idle && <Notice>{t('common.available_when_idle')}</Notice>}
      {idle && !homed && <Notice>{t('control.motion_requires_homing_commands')}</Notice>}
      {own && (
        <p role="status" class="mt-3 text-xs text-muted">
          {t(
            status.busy
              ? 'control.shaper_running'
              : status.completed && status.success
                ? 'control.shaper_completed'
                : 'control.shaper_check_output'
          )}
        </p>
      )}
      {own && status.output && (
        <pre class="mt-3 max-h-40 overflow-auto whitespace-pre-wrap break-words text-xs">{status.output}</pre>
      )}
    </Card>
  )
}

type PidStatus = {
  busy: boolean
  heater: string
  ready: boolean
  failed: boolean
  kp: number | null
  ki: number | null
  kd: number | null
}
const pidStatus = store({ data: null as PidStatus | null, ok: false })
const refreshPid = async () => {
  try {
    pidStatus.set({ data: await request('/api/pid'), ok: true })
  } catch {
    pidStatus.set({ ok: false })
  }
}
const PidCalibration = ({ d }: { d: any }) => {
  usePoll(refreshPid, 3000)
  const { data: status, ok } = pidStatus.use()
  const connected = printer.use().ok
  const [hotend, setHotend] = useState('200'),
    [bed, setBed] = useState('60')
  const [sending, setSending] = useState(false)
  const lock = useRef(false)
  const idle =
    connected && d?.connected && d?.machine?.status === 1 && d?.last_message_age >= 0 && d?.last_message_age <= 15
  const can = idle && ok && !status?.busy && !sending
  const send = async (heater: string, target: string) => {
    if (lock.current || !can) return
    const value = Number(target),
      isHotend = heater === 'extruder'
    if (!target.trim() || !Number.isFinite(value) || value < (isHotend ? 150 : 40) || value > (isHotend ? 300 : 120))
      return notify(t('control.pid_invalid_target'), 'error')
    lock.current = true
    setSending(true)
    try {
      if (
        await control(
          `pid:${heater}:${value}`,
          tpl('control.pid_confirm', { heater: t(isHotend ? 'common.nozzle' : 'common.heated_bed'), target: value })
        )
      )
        await refreshPid()
    } finally {
      lock.current = false
      setSending(false)
    }
  }
  const save = async () => {
    if (lock.current || !can || !status?.ready) return
    lock.current = true
    setSending(true)
    try {
      if (await control('pid:save', t('control.pid_save_confirm'))) await refreshPid()
    } finally {
      lock.current = false
      setSending(false)
    }
  }
  return (
    <Card class="cc2-pid">
      <CardHead icon="temp" title="control.pid_title" />
      {(
        [
          ['extruder', 'common.nozzle', hotend, setHotend, 150, 300],
          ['heater_bed', 'common.heated_bed', bed, setBed, 40, 120],
        ] as const
      ).map(([heater, label, value, set, min, max]) => (
        <div key={heater} class="my-3 grid grid-cols-[minmax(0,1fr)_6rem_auto] items-center gap-2">
          <span>{t(label)}</span>
          <label class="flex items-center gap-1">
            <Input
              type="number"
              min={min}
              max={max}
              value={value}
              disabled={!can}
              aria-label={tpl('control.pid_target', { heater: t(label) })}
              onInput={e => set(e.currentTarget.value)}
            />
            <small>°C</small>
          </label>
          <Button
            disabled={!can}
            onClick={() => send(heater, value)}
            aria-label={tpl('control.pid_start', { heater: t(label) })}
          >
            {t('control.pid_calibrate')}
          </Button>
        </div>
      ))}
      <p role="status" class="text-xs text-muted">
        {t(
          !ok
            ? 'control.pid_unavailable'
            : status?.busy
              ? 'control.pid_running'
              : status?.ready
                ? 'control.pid_completed'
                : status?.failed
                  ? 'control.pid_failed'
                  : 'control.pid_help'
        )}
      </p>
      {ok && status?.ready && (
        <div class="mt-3 rounded border border-edge p-2 text-xs">
          <strong>{t(status.heater === 'extruder' ? 'common.nozzle' : 'common.heated_bed')}</strong>
          <div class="mt-1 grid grid-cols-3 gap-2">
            {(['kp', 'ki', 'kd'] as const).map(k => (
              <span key={k}>
                {k.toUpperCase()} <strong>{status[k] === null ? '—' : status[k].toFixed(3)}</strong>
              </span>
            ))}
          </div>
        </div>
      )}
      <Button wide class="mt-3" disabled={!can || !status?.ready} onClick={save}>
        {t('control.pid_save')}
      </Button>
      {!idle && <Notice>{t('common.available_when_idle')}</Notice>}
    </Card>
  )
}

const Extruder = ({ d, v }: { d: any; v: ReturnType<typeof view> }) => {
  const [len, setLen] = useState('10')
  const ok = v.idle && Number(d?.extruder?.temperature) >= 170
  return (
    <Card>
      <CardHead icon="control" title="control.extruder" />
      {!ok && (
        <Notice>
          <span>
            {t('control.extrusion_disabled')}
            <br />
            <small>{t('control.idle_only_nozzle_temperature_170_c')}</small>
          </span>
        </Notice>
      )}
      <div class="my-4 flex items-center gap-2 text-xs">
        {t('control.length')}{' '}
        <Select
          class="w-16"
          value={len}
          aria-label={t('control.extrusion_length')}
          onChange={e => setLen(e.currentTarget.value)}
        >
          {[5, 10, 25].map(n => (
            <option key={n}>{n}</option>
          ))}
        </Select>{' '}
        mm
      </div>
      <div class="grid grid-cols-2 gap-2.5">
        <Button class="min-h-14" disabled={!ok} onClick={() => control(`extrude:${len}`)}>
          <ArrowBigUp {...I} />
          {t('control.extrude')}
        </Button>
        <Button class="min-h-14" disabled={!ok} onClick={() => control(`extrude:${-Number(len)}`)}>
          <ArrowBigDown {...I} />
          {t('control.retract')}
        </Button>
      </div>
    </Card>
  )
}

const ZOffset = () => {
  const { v: off, reference, pending, timedOut } = zoffset.use()
  const lock = useRef(false)
  const [busy, setBusy] = useState(false)
  const send = async (action: string) => {
    if (lock.current || pending || off === null || reference === null) return
    lock.current = true
    setBusy(true)
    try {
      if (await control(action)) zoffset.set({ pending: true })
      await refreshPrinter()
    } finally {
      lock.current = false
      setBusy(false)
    }
  }
  const adjust = (delta: number) => {
    if (off !== null && reference !== null && Math.abs(off + delta - reference) > 0.5001)
      return notify(t('control.session_z_offset_is_limited_to'), 'error')
    return send(`zoffset:adjust:${delta}`)
  }
  const undo = () =>
    off !== null && reference !== null && Math.abs(off - reference) >= 0.0001 && send(`zoffset:undo:${reference - off}`)
  return (
    <Card>
      <CardHead icon="z" title="common.live_z_offset" end={<Tag tone="warning">{t('common.session_only')}</Tag>} />
      <small class="text-muted">{t('control.protected_session_adjustment')}</small>
      <div class="my-2 text-3xl">
        {off === null ? '—' : (off > 0 ? '+' : '') + off.toFixed(2)} <small class="text-sm text-muted">mm</small>
      </div>
      <div class="flex gap-2">
        {[-0.05, -0.01, 0.01, 0.05].map(dv => (
          <Button
            key={dv}
            class="flex-1 text-xs"
            disabled={busy || pending || off === null || reference === null}
            onClick={() => adjust(dv)}
          >
            {dv < 0 ? '−' : '+'} {Math.abs(dv).toFixed(2)}
          </Button>
        ))}
      </div>
      <Button
        wide
        class="mt-2"
        disabled={busy || pending || off === null || reference === null || Math.abs(off - reference) < 0.0001}
        onClick={undo}
      >
        {t('control.undo_session_offset')}
      </Button>
      <Warn>{t('control.live_session_adjustment_it_resets')}</Warn>
      {timedOut && <Notice>{t('control.z_offset_readback_timeout')}</Notice>}
    </Card>
  )
}

const Profiles = () => {
  const list = presets.use().list
  const [idx, setIdx] = useState(0),
    [name, setName] = useState(''),
    [nozzle, setNozzle] = useState(''),
    [bed, setBed] = useState('')
  const load = (i: number) => {
    const p = list[i]
    if (p) {
      setIdx(i)
      setName(p.name)
      setNozzle(String(p.nozzle))
      setBed(String(p.bed))
    }
  }
  useEffect(() => load(Math.min(idx, list.length - 1)), [list])
  const save = async () => {
    const n = name.trim().toUpperCase().slice(0, 16),
      nz = Number(nozzle),
      b = Number(bed)
    if (
      !nozzle.trim() ||
      !bed.trim() ||
      !Number.isFinite(nz) ||
      !Number.isFinite(b) ||
      !/^[A-Z0-9+_-]{1,16}$/.test(n) ||
      nz < 0 ||
      nz > 300 ||
      b < 0 ||
      b > 120
    )
      return notify(t('control.invalid_material_profile_values'), 'error')
    const at = list.findIndex(p => p.name.toUpperCase() === n),
      prev = at >= 0 ? list[at] : null
    const item = {
      name: n,
      nozzle: Math.round(nz),
      bed: Math.round(b),
      min: prev?.min || Math.max(120, Math.round(nz - 20)),
      max: prev?.max || Math.min(320, Math.round(nz + 20)),
    }
    const next = at >= 0 ? list.map((p, i) => (i === at ? item : p)) : [...list, item]
    try {
      await savePresets(next)
      presets.set({ list: next })
      setIdx(at >= 0 ? at : next.length - 1)
      notify(tpl('control.saved_name_on_the_printer', { name: n }))
    } catch (e) {
      notify(tpl('control.profile_save_failed_error', { error: errText(e) }), 'error')
    }
  }
  const remove = async () => {
    if (list.length <= 1) return notify(t('control.at_least_one_profile_must_remain'), 'error')
    const next = list.filter((_, i) => i !== idx),
      gone = list[idx]
    try {
      await savePresets(next)
      presets.set({ list: next })
      notify(tpl('control.deleted_name', { name: gone.name }))
    } catch (e) {
      notify(tpl('control.profile_deletion_failed_error', { error: errText(e) }), 'error')
    }
  }
  const lab = 'text-xs text-muted'
  return (
    <Card class="cc2-material-presets">
      <CardHead icon="temp" title="control.material_profiles" end={t('control.stored_on_the_printer')} />
      <div class="grid grid-cols-2 items-end gap-2.5">
        <label class={lab}>
          {t('control.profile')}
          <Select class="mt-1.5" value={String(idx)} onChange={e => load(+e.currentTarget.value)}>
            {list.map((p, i) => (
              <option key={i} value={i}>
                {p.name} · {p.nozzle}/{p.bed}
              </option>
            ))}
          </Select>
        </label>
        <label class={lab}>
          {t('common.name')}
          <Input class="mt-1.5" maxLength={16} value={name} onInput={e => setName(e.currentTarget.value)} />
        </label>
        <label class={lab}>
          {t('control.nozzle_c')}
          <Input
            class="mt-1.5"
            type="number"
            min="0"
            max="300"
            value={nozzle}
            onInput={e => setNozzle(e.currentTarget.value)}
          />
        </label>
        <label class={lab}>
          {t('control.bed_c')}
          <Input
            class="mt-1.5"
            type="number"
            min="0"
            max="120"
            value={bed}
            onInput={e => setBed(e.currentTarget.value)}
          />
        </label>
        <Button variant="primary" onClick={save}>
          {t('control.add_update')}
        </Button>
        <Button variant="danger" onClick={remove}>
          {t('common.delete')}
        </Button>
      </div>
      <p class="mt-2.5 text-muted">{t('control.profiles_are_available_from_every')}</p>
    </Card>
  )
}

export const Control = () => {
  usePoll(refreshPrinter, 1500)
  const d = printer.use().data
  const v = view(d)
  const tag = (s: string) => (
    <Tag tone="warning">
      <Pip /> {s}
    </Tag>
  )
  return (
    <Page
      title="common.control"
      sub="control.movement_temperatures_and_machine"
      tags={
        <>
          {tag(d ? stateText(v) : t('common.connecting'))}
          {tag(v.homed ? `${v.homed.toUpperCase()} ${t('common.homed')}` : t('common.not_homed'))}
        </>
      }
    >
      <div class="cc2-control-columns grid gap-3.5 cc2-lg:grid-cols-2 cc2-xl:grid-cols-3">
        <div class="grid content-start gap-3.5 cc2-xl:grid-rows-[1fr]">
          <Movement v={v} />
        </div>
        <div class="grid content-start gap-3.5 cc2-xl:grid-rows-[auto_auto_1fr]">
          <Temperatures d={d} v={v} />
          <Card>
            <CardHead icon="fan" title="common.fans" />
            {(['part', 'aux', 'box'] as const).map(k => (
              <FanSlider
                key={k}
                label={k === 'part' ? 'common.part_fan' : k === 'aux' ? 'common.aux_fan' : 'control.chamber_fan'}
                pct={v.fan(k)}
                onCommit={n => control(`fan:${k}:${n}`)}
              />
            ))}
          </Card>
        </div>
        <div class="grid content-start gap-3.5 cc2-lg:col-span-2 cc2-lg:grid-cols-2 cc2-xl:col-span-1 cc2-xl:grid-cols-1 cc2-xl:grid-rows-[auto_1fr]">
          <Card>
            <CardHead icon="settings" title="control.machine" />
            <div class="grid auto-rows-fr grid-cols-2 gap-2.5">
              <Button
                class="min-h-16 flex-col gap-1"
                variant={v.lightOn ? 'active' : 'default'}
                aria-pressed={v.lightOn}
                title={t(
                  v.lightOn ? 'control.internal_light_on_press_to_turn' : 'control.internal_light_off_press_to_turn'
                )}
                onClick={async () => {
                  if (await control(v.lightOn ? 'light:off' : 'light:on')) setTimeout(refreshPrinter, 250)
                }}
              >
                <Icon n="light" class="text-cyan" />
                <span>
                  {t('common.lights')} {t(v.lightOn ? 'control.on' : 'control.off')}
                </span>
              </Button>
              <Button
                class="min-h-16 flex-col gap-1"
                disabled={!v.idle}
                onClick={() => control('system:motors_off', t('common.disable_all_motors'))}
              >
                <Icon n="motors" class="text-cyan" />
                {t('common.motors_off')}
              </Button>
              <Button
                class="min-h-16 flex-col gap-1"
                disabled={!v.idle}
                onClick={() => control('system:heaters_off', t('common.turn_all_heaters_off'))}
              >
                <Icon n="temp" class="text-cyan" />
                {t('control.heaters_off')}
              </Button>
              <Button
                class="min-h-16 flex-col gap-1"
                disabled={!v.idle}
                onClick={() => control('system:fans_off', t('common.turn_all_fans_off'))}
              >
                <Icon n="fan" class="text-cyan" />
                {t('common.fans_off')}
              </Button>
            </div>
            <Notice icon="lock">
              <span>
                {t('control.protected_actions')}
                <br />
                <small>{t('control.emergency_stop_is_always_available')}</small>
              </span>
            </Notice>
          </Card>
          <ZOffset />
        </div>
      </div>
      <div class="cc2-control-calibrations mt-3.5 grid items-start gap-3.5 cc2-xl:grid-cols-[minmax(0,1fr)_minmax(0,1.2fr)_minmax(0,1fr)]">
        <InputShaper d={d} v={v} />
        <PidCalibration d={d} />
        <PrintTuning compact />
      </div>
      <div class="cc2-control-bottom mt-3.5 grid items-start gap-3.5 cc2-lg:grid-cols-2 cc2-xl:grid-cols-3">
        <Extruder d={d} v={v} />
        <div class="cc2-xl:col-span-2">
          <Profiles />
        </div>
      </div>
    </Page>
  )
}
