import { useEffect, useState } from 'preact/hooks'
import { ArrowBigDown, ArrowBigLeft, ArrowBigRight, ArrowBigUp, ArrowUpRight } from 'lucide-preact'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Dot, Pip, Tag } from '@/components/ui/badge'
import { Input, Select } from '@/components/ui/field'
import { Icon } from '@/components/icons'
import { FanSlider, Notice, Warn } from '@/components/shared'
import { control, errText, notify } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { num } from '@/lib/format'
import { openPage, presets, printer, refreshPrinter, savePresets, view, zoffset } from '@/lib/state'

const STEPS = [0.1, 1, 10, 50]
const I = { size: 16, strokeWidth: 1 }
const B = { size: 22, strokeWidth: 1 }

const Movement = ({ v }: { v: ReturnType<typeof view> }) => {
  const [step, setStep] = useState(0.1)
  const can = (axis: string) => v.idle && v.homed.includes(axis)
  const move = (axis: string, dir: number) => control(`move:${axis}:${dir * step}`)
  const pad = 'min-h-11 text-xl'
  return (
    <Card>
      <CardHead icon="control" title="Movement" />
      <Button wide class="min-h-12" disabled={!v.idle} onClick={() => control('home:ALL')}><Icon n="home" />{t('Home All')}</Button>
      <div class="mt-3 grid grid-cols-3 gap-2.5">
        {['X', 'Y', 'Z'].map(a => <Button key={a} class="min-h-16 flex-col gap-1" disabled={!v.idle} onClick={() => control(`home:${a}`)}><Icon n="home" class="text-cyan" />{t(`Home ${a}`)}</Button>)}
      </div>
      <div class="my-4 grid grid-cols-[1.4fr_.65fr] gap-5 border-t border-edge pt-4">
        <div><div class="mb-2 text-[13px]">{t('XY Move')}</div>
          <div class="grid grid-cols-3 gap-1.5">
            <span /><Button class={pad} aria-label="Move Y positive" disabled={!can('y')} onClick={() => move('Y', 1)}><ArrowBigUp {...B} /></Button><span />
            <Button class={pad} aria-label="Move X negative" disabled={!can('x')} onClick={() => move('X', -1)}><ArrowBigLeft {...B} /></Button><span />
            <Button class={pad} aria-label="Move X positive" disabled={!can('x')} onClick={() => move('X', 1)}><ArrowBigRight {...B} /></Button><span />
            <Button class={pad} aria-label="Move Y negative" disabled={!can('y')} onClick={() => move('Y', -1)}><ArrowBigDown {...B} /></Button>
          </div></div>
        <div class="border-l border-edge pl-5"><div class="mb-2 text-[13px]">{t('Z Move')}</div>
          <div class="grid gap-1.5"><Button class={pad} aria-label="Move Z positive" disabled={!can('z')} onClick={() => move('Z', 1)}><ArrowBigUp {...B} /></Button><Button class={pad} aria-label="Move Z negative" disabled={!can('z')} onClick={() => move('Z', -1)}><ArrowBigDown {...B} /></Button></div></div>
      </div>
      <small class="text-muted">{t('Step Size')}</small>
      <div class="my-2 flex gap-2">{STEPS.map(s => <Button key={s} variant={s === step ? 'active' : 'default'} class="flex-1 text-xs" onClick={() => setStep(s)}>{s} mm</Button>)}</div>
      <Notice>{t('Motion requires homing. Commands are disabled until the printer is ready.')}</Notice>
      <div class="mt-4 border-t border-edge pt-3"><small class="text-muted">{t('Current Position')}</small>
        <div class="my-2 grid grid-cols-3">{(['x', 'y', 'z'] as const).map(a => <div key={a}><small class="text-muted">{a.toUpperCase()}</small><strong class="mt-1 block text-[15px]">{v.pos(a)}</strong></div>)}</div>
        <small class="text-muted">{t('Homed Status')}</small>
        <div class="mt-2 grid grid-cols-3">{(['x', 'y', 'z'] as const).map(a => { const ok = v.homed.includes(a); return <small key={a} class={ok ? 'text-cyan' : 'text-muted'}><Pip hollow={!ok} /> {a.toUpperCase()} {t(ok ? 'Homed' : 'not homed')}</small> })}</div></div>
    </Card>
  )
}

const Temperatures = ({ d }: { d: any }) => {
  const list = presets.use().list
  const [nozzle, setNozzle] = useState('0'), [bed, setBed] = useState('0'), [active, setActive] = useState('')
  const apply = async () => {
    const n = Number(nozzle), b = Number(bed)
    if (!Number.isFinite(n) || n < 0 || n > 300 || !Number.isFinite(b) || b < 0 || b > 120) return notify(t('Invalid temperature target.'))
    await control(`preheat:${n}:${b}`)
  }
  return (
    <Card>
      <CardHead icon="temp" title="Temperatures" />
      {([['red', 'Nozzle', d?.extruder?.temperature, nozzle, setNozzle, 300], ['blue', 'Heated Bed', d?.heater_bed?.temperature, bed, setBed, 120]] as const).map(([dot, label, cur, val, set, max]) => (
        <div key={label} class="my-4 flex items-center gap-2.5"><Dot c={dot} /><span>{t(label)}</span><strong class="ml-auto text-[15px]">{num(cur)} °C</strong>
          <Input class="w-18 text-center" type="number" min="0" max={max} value={val} aria-label={`${t(label)} target`} onInput={e => set(e.currentTarget.value)} /><small>°C</small></div>
      ))}
      <Button wide onClick={apply}>{t('Apply targets')}</Button>
      <div class="mt-4 border-t border-edge pt-3"><small class="text-muted">{t('Temperature Presets')}</small>
        <div class="mt-2 flex flex-wrap gap-2">{list.map(p => (
          <Button key={p.name} class="min-w-20 flex-1 text-xs" variant={active === p.name ? 'active' : 'default'} title={`${p.nozzle} °C nozzle / ${p.bed} °C bed`}
            onClick={() => { setNozzle(String(p.nozzle)); setBed(String(p.bed)); setActive(p.name); notify(tpl('{name}: targets {nozzle}/{bed} °C loaded. Press Apply targets to send them.', { name: p.name, nozzle: p.nozzle, bed: p.bed })) }}>{p.name}</Button>
        ))}</div></div>
    </Card>
  )
}

const Extruder = ({ d, v }: { d: any; v: ReturnType<typeof view> }) => {
  const [len, setLen] = useState('10')
  const ok = v.idle && Number(d?.extruder?.temperature) >= 170
  return (
    <Card>
      <CardHead icon="control" title="Extruder" />
      <Notice><span>{t('Extrusion disabled')}<br /><small>{t('Idle only · nozzle temperature ≥ 170 °C')}</small></span></Notice>
      <div class="my-4 flex items-center gap-2 text-xs">{t('Length')} <Select class="w-16" value={len} aria-label="Extrusion length" onChange={e => setLen(e.currentTarget.value)}>{[5, 10, 25].map(n => <option key={n}>{n}</option>)}</Select> mm</div>
      <div class="grid grid-cols-2 gap-2.5"><Button class="min-h-14" disabled={!ok} onClick={() => control(`extrude:${len}`)}><ArrowBigUp {...I} />{t('Extrude')}</Button><Button class="min-h-14" disabled={!ok} onClick={() => control(`extrude:${-Number(len)}`)}><ArrowBigDown {...I} />{t('Retract')}</Button></div>
    </Card>
  )
}

const ZOffset = () => {
  const off = zoffset.use().v
  const adjust = async (delta: number) => {
    const next = Math.round((off + delta) * 100) / 100
    if (Math.abs(next) > 0.5001) return notify(t('Session Z offset is limited to ±0.50 mm.'))
    if (await control(`zoffset:adjust:${delta}`)) zoffset.set({ v: next })
  }
  const undo = async () => { const u = -off; if (Math.abs(u) < 0.0001 || (await control(`zoffset:undo:${u}`))) zoffset.set({ v: 0 }) }
  return (
    <Card>
      <CardHead icon="z" title="Live Z Offset" end={<Tag tone="warning">{t('Session only')}</Tag>} />
      <small class="text-muted">{t('Protected session adjustment')}</small>
      <div class="my-2 text-3xl">{(off > 0 ? '+' : '') + off.toFixed(2)} <small class="text-sm text-muted">mm</small></div>
      <div class="flex gap-2">{[-0.05, -0.01, 0.01, 0.05].map(dv => <Button key={dv} class="flex-1 text-xs" onClick={() => adjust(dv)}>{dv < 0 ? '−' : '+'} {Math.abs(dv).toFixed(2)}</Button>)}</div>
      <Button wide class="mt-2" onClick={undo}>{t('Undo session offset')}</Button>
      <Warn>{t('Live session adjustment. It resets after restart and does not modify saved settings. Session limit: ±0.50 mm.')}</Warn>
    </Card>
  )
}

const Profiles = () => {
  const list = presets.use().list
  const [idx, setIdx] = useState(0), [name, setName] = useState(''), [nozzle, setNozzle] = useState(''), [bed, setBed] = useState('')
  const load = (i: number) => { const p = list[i]; if (p) { setIdx(i); setName(p.name); setNozzle(String(p.nozzle)); setBed(String(p.bed)) } }
  useEffect(() => load(Math.min(idx, list.length - 1)), [list])
  const save = async () => {
    const n = name.trim().toUpperCase().slice(0, 16), nz = Number(nozzle), b = Number(bed)
    if (!/^[A-Z0-9+_-]{1,16}$/.test(n) || nz < 0 || nz > 300 || b < 0 || b > 120) return notify(t('Invalid material profile values.'))
    const at = list.findIndex(p => p.name.toUpperCase() === n), prev = at >= 0 ? list[at] : null
    const item = { name: n, nozzle: Math.round(nz), bed: Math.round(b), min: prev?.min || Math.max(120, Math.round(nz - 20)), max: prev?.max || Math.min(320, Math.round(nz + 20)) }
    const next = at >= 0 ? list.map((p, i) => (i === at ? item : p)) : [...list, item]
    try { await savePresets(next); presets.set({ list: next }); setIdx(at >= 0 ? at : next.length - 1); notify(tpl('Saved {name} on the printer.', { name: n })) } catch (e) { notify(tpl('Profile save failed: {error}', { error: errText(e) })) }
  }
  const remove = async () => {
    if (list.length <= 1) return notify(t('At least one profile must remain.'))
    const next = list.filter((_, i) => i !== idx), gone = list[idx]
    try { await savePresets(next); presets.set({ list: next }); notify(tpl('Deleted {name}.', { name: gone.name })) } catch (e) { notify(tpl('Profile deletion failed: {error}', { error: errText(e) })) }
  }
  const lab = 'text-[11px] text-muted'
  return (
    <Card class="mt-3.5">
      <CardHead icon="temp" title="Material Profiles" end={t('Stored on the printer')} />
      <div class="grid grid-cols-2 items-end gap-2.5 md:grid-cols-[1.1fr_1.1fr_.7fr_.7fr_auto_auto]">
        <label class={lab}>{t('Profile')}<Select class="mt-1.5" value={String(idx)} onChange={e => load(+e.currentTarget.value)}>{list.map((p, i) => <option key={i} value={i}>{p.name} · {p.nozzle}/{p.bed}</option>)}</Select></label>
        <label class={lab}>{t('Name')}<Input class="mt-1.5" maxLength={16} value={name} onInput={e => setName(e.currentTarget.value)} /></label>
        <label class={lab}>{t('Nozzle °C')}<Input class="mt-1.5" type="number" min="0" max="300" value={nozzle} onInput={e => setNozzle(e.currentTarget.value)} /></label>
        <label class={lab}>{t('Bed °C')}<Input class="mt-1.5" type="number" min="0" max="120" value={bed} onInput={e => setBed(e.currentTarget.value)} /></label>
        <Button variant="primary" onClick={save}>{t('Add / update')}</Button><Button variant="danger" onClick={remove}>{t('Delete')}</Button>
      </div>
      <p class="mt-2.5 text-muted">{t('Profiles are available from every browser and are also used for Canvas material assignment.')}</p>
    </Card>
  )
}

export const Control = () => {
  const d = printer.use().data
  const v = view(d)
  const tag = (s: string) => <Tag tone="warning"><Pip /> {s}</Tag>
  return (
    <Page title="Control" sub="Movement, temperatures and machine controls" tags={<>{tag(t(d ? v.state : 'Connecting…'))}{tag(v.homed ? `${v.homed.toUpperCase()} ${t('homed')}` : t('Not homed'))}</>}>
      <div class="grid gap-3.5 lg:grid-cols-2 xl:grid-cols-3">
        <div class="grid content-start gap-3.5"><Movement v={v} /></div>
        <div class="grid content-start gap-3.5">
          <Temperatures d={d} />
          <Card><CardHead icon="fan" title="Fans" />{(['part', 'aux', 'box'] as const).map(k => <FanSlider key={k} label={k === 'part' ? 'Part Fan' : k === 'aux' ? 'Aux Fan' : 'Chamber Fan'} pct={v.fan(k)} onCommit={n => control(`fan:${k}:${n}`)} />)}</Card>
          <Extruder d={d} v={v} />
        </div>
        <div class="grid content-start gap-3.5 lg:col-span-2 lg:grid-cols-2 xl:col-span-1 xl:grid-cols-1">
          <Card><CardHead icon="file" title="Print Status" />
            <div class="mt-1 text-2xl font-semibold [overflow-wrap:anywhere]">{v.rawFilename === 'No active file' ? t('No active print') : v.rawFilename}</div>
            <p class="my-3 text-[13px] text-muted">{t('Pause, resume, cancel and object exclusion are grouped in the Job page.')}</p>
            <Button wide onClick={() => openPage('job')}>{t('Open Current Job')}<ArrowUpRight {...I} /></Button></Card>
          <Card><CardHead icon="settings" title="Machine" />
            <div class="grid grid-cols-2 gap-2.5">
              <Button class={`min-h-16 flex-col gap-1 ${v.lightOn ? 'border-cyan text-cyan' : 'text-muted'}`} aria-pressed={v.lightOn} title={t(v.lightOn ? 'Internal light on: press to turn off' : 'Internal light off: press to turn on')}
                onClick={async () => { if (await control(v.lightOn ? 'light:off' : 'light:on')) setTimeout(refreshPrinter, 250) }}><Icon n="light" class="text-current" /><span>{t('Lights')}</span><small class="text-[10px] opacity-80">{t(v.lightOn ? 'On' : 'Off')}</small></Button>
              <Button class="min-h-16 flex-col gap-1" onClick={() => control('system:motors_off', t('Disable all motors?'))}><Icon n="motors" class="text-cyan" />{t('Motors Off')}</Button>
              <Button class="min-h-16 flex-col gap-1" onClick={() => control('system:heaters_off', t('Turn all heaters off?'))}><Icon n="temp" class="text-cyan" />{t('Heaters Off')}</Button>
              <Button class="min-h-16 flex-col gap-1" onClick={() => control('system:fans_off', t('Turn all fans off?'))}><Icon n="fan" class="text-cyan" />{t('Fans Off')}</Button>
            </div>
            <Notice icon="lock"><span>{t('Protected actions')}<br /><small>{t('Emergency Stop is always available in the sidebar.')}</small></span></Notice></Card>
          <ZOffset />
        </div>
      </div>
      <Profiles />
    </Page>
  )
}
