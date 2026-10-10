import { useEffect, useState } from 'preact/hooks'
import { ask } from '@/lib/confirm'
import { ArrowBigDown, ArrowBigUp, Check, Info, Palette, RefreshCw, Repeat } from 'lucide-preact'
import { cn } from '@/lib/utils'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Pip, Tag } from '@/components/ui/badge'
import { Select } from '@/components/ui/field'
import { Dialog } from '@/components/ui/dialog'
import { Row } from '@/components/shared'
import { control, errText, notify, post } from '@/lib/api'
import { type Key, t, tpl } from '@/lib/i18n'
import { poll, usePoll } from '@/lib/poll'
import { canvas, canvasColour, canvasHex, refreshCanvas } from '@/lib/canvas'
import { presets, printer, view } from '@/lib/state'
import { findSpool, g, openChooser, refreshSpools, spoolLabel, spools } from '@/lib/spools'

const I = { size: 16, strokeWidth: 1 }
const PALETTE = [
  '#F5F5F5',
  '#B0BEC5',
  '#616E77',
  '#16191D',
  '#EF5350',
  '#FF8A3D',
  '#FDD835',
  '#66BB6A',
  '#00CFE8',
  '#2196F3',
  '#3F51B5',
  '#AB47BC',
  '#EC407A',
  '#795548',
  '#C7A44A',
]

const MaterialDialog = ({ slot, onClose }: { slot: number; onClose: () => void }) => {
  const list = presets.use().list
  const tray = canvas.get().model?.trays[slot]
  const [preset, setPreset] = useState(0),
    [colour, setColour] = useState(canvasHex(tray?.filament_color, slot))
  const submit = async (e: Event) => {
    e.preventDefault()
    const p = list[preset]
    if (!p) return
    const hex = colour.slice(1).toUpperCase(),
      min = Number(p.min || Math.max(120, p.nozzle - 20)),
      max = Number(p.max || Math.min(320, p.nozzle + 20))
    if (
      !(await ask(
        tpl('canvas.save_name_colour_min_max_c', {
          name: p.name,
          colour: hex,
          min,
          max,
          slot: slot + 1,
        })
      ))
    )
      return
    canvas.set(s => ({
      optimistic: { ...s.optimistic, [slot]: { colour: `#${hex}`, material: p.name, until: Date.now() + 4000 } },
    }))
    onClose()
    if (await control(`canvas:material:${slot}:${p.name}:${hex}:${min}:${max}`)) {
      setTimeout(refreshCanvas, 700)
      setTimeout(refreshCanvas, 2000)
      // With spool tracking on, the second question: which spool now carries this filament?
      if (spools.get().data?.enabled) openChooser(slot, 'material', { material: p.name, color: `#${hex}` })
    } else refreshCanvas()
  }
  return (
    <Dialog onClose={onClose}>
      <form onSubmit={submit}>
        <h2 class="mb-4 text-xl font-semibold">{t('canvas.choose_material_and_colour')}</h2>
        <label htmlFor="material-profile" class="mb-1.5 mt-3 block text-muted">
          {t('canvas.material_profile')}
        </label>
        <Select
          id="material-profile"
          value={String(preset)}
          onChange={e => setPreset(+e.currentTarget.value)}
          autofocus
        >
          {list.map((p, i) => (
            <option key={i} value={i}>
              {p.name} ({p.min || p.nozzle - 20}–{p.max || p.nozzle + 20} °C)
            </option>
          ))}
        </Select>
        <label htmlFor="material-colour" class="mb-1.5 mt-3 block text-muted">
          {t('canvas.filament_colour')}
        </label>
        <div class="grid grid-cols-[78px_1fr] items-center gap-3">
          <input
            id="material-colour"
            type="color"
            class="h-12 w-19.5 rounded-md border border-edge bg-field p-0.5"
            value={colour.toLowerCase()}
            onInput={e => setColour(e.currentTarget.value.toUpperCase())}
          />
          <output class="font-mono text-base font-semibold">{colour}</output>
        </div>
        <fieldset class="m-0 min-w-0 border-0 p-0">
          <legend class="mb-1.5 mt-3 p-0 text-muted">{t('canvas.quick_colours')}</legend>
          <div class="grid grid-cols-5 gap-2 cc2-sm:grid-cols-8">
            {PALETTE.map(c => (
              <button
                key={c}
                type="button"
                title={c}
                class={cn(
                  'h-8.5 rounded-md border-2 border-white/30',
                  c === colour && 'outline outline-2 outline-offset-2 outline-cyan'
                )}
                style={{ background: c }}
                onClick={() => setColour(c)}
              />
            ))}
          </div>
        </fieldset>
        <div class="mt-5 flex justify-end gap-2.5">
          <Button onClick={onClose}>{t('common.cancel')}</Button>
          <Button type="submit" variant="primary">
            {t('canvas.apply_to_canvas_slot')}
          </Button>
        </div>
      </form>
    </Dialog>
  )
}

export const Canvas = () => {
  const { model, autoRefill, slot, optimistic, checked, eject } = canvas.use()
  const machine = printer.use().data
  const idle = view(machine).idle && !view(machine).paused && machine?.connected && machine.last_message_age <= 15
  const [ejectPending, setEjectPending] = useState(false)
  const library = spools.use().data
  const tracking = Boolean(library?.available && library.enabled)
  const [dialog, setDialog] = useState(false)
  const [refillBusy, setRefillBusy] = useState(false)
  // Refresh while the page is open, except behind the material dialog.
  useEffect(() => (dialog ? undefined : poll(refreshCanvas, 2000)), [dialog])
  usePoll(refreshSpools, 10000)
  const ok = Boolean(model?.connected)
  const startEject = async () => {
    if (ejectPending || eject.running || !eject.available || !idle) return
    setEjectPending(true)
    try {
      await control(`canvas:eject:${slot}`, tpl('canvas.eject_confirm', { slot: slot + 1 }))
      await refreshCanvas()
    } finally {
      setEjectPending(false)
    }
  }
  const cancelEject = async () => {
    try {
      await post('/api/canvas/eject/cancel')
      notify(t('canvas.eject_stop_requested'))
    } catch (e) {
      notify(errText(e), 'error')
    }
  }
  const ejectResults: Record<string, Key> = {
    unavailable: 'canvas.eject_unavailable',
    running: 'canvas.eject_running',
    complete: 'canvas.eject_complete',
    empty: 'canvas.eject_empty',
    cancelled: 'canvas.eject_cancelled',
    disconnected: 'canvas.eject_disconnected',
    stale: 'canvas.eject_stale',
    blocked: 'canvas.eject_blocked',
    fault: 'canvas.eject_fault',
    stalled: 'canvas.eject_stalled',
    travel_limit: 'canvas.eject_travel_limit',
    timed_out: 'canvas.eject_timed_out',
    command_failed: 'canvas.eject_command_failed',
    stop_failed: 'canvas.eject_stop_failed',
  }
  const sync = async () => {
    try {
      await post('/api/canvas/refresh')
      setTimeout(refreshCanvas, 600)
    } catch (e) {
      notify(errText(e), 'error')
    }
  }
  // Readback comes from the printer: CC2 Control asks for the Canvas state again once the change is accepted.
  const toggleRefill = async () => {
    if (autoRefill === null) return
    setRefillBusy(true)
    try {
      await post('/api/canvas/auto-refill', autoRefill ? 'off' : 'on')
      notify(t('common.command_accepted'))
      setTimeout(refreshCanvas, 1500)
    } catch (e) {
      notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
    } finally {
      setRefillBusy(false)
    }
  }
  const slots = [0, 1, 2, 3].map(i => {
    const tray = model?.trays[i],
      tel = tray?.filament_color
    const saved = optimistic[i]
    const pend = saved && Date.now() < saved.until && canvasHex(tel, i) !== saved.colour ? saved : undefined // drop it once expired or confirmed by telemetry
    const raw = pend ? pend.colour : tel
    // With tracking on, the remaining weight is the spool's count; the printer reports none.
    const held = tracking && library ? library.slots[i] : null
    const spool = held ? findSpool(library, held.spool) : null
    return {
      i,
      colour: canvasColour(raw, i),
      raw,
      material: pend ? pend.material : (tray && (tray.filament_name || tray.filament_type)) || '—',
      remaining: spool
        ? `${g(spool.remaining)} g`
        : tray && tray.remaining_percent !== undefined
          ? `${tray.remaining_percent}%`
          : '—',
      held,
      spool,
    }
  })
  const ctl: [typeof ArrowBigUp, Key, () => void, string][] = [
    [
      ArrowBigUp,
      'canvas.load',
      () => control(`canvas:load:${slot}`, tpl('canvas.load_filament_from_canvas_slot', { slot: slot + 1 })) as any,
      '',
    ],
    [
      ArrowBigDown,
      'canvas.unload',
      () =>
        control(
          `canvas:unload:${slot}`,
          tpl('canvas.unload_filament_from_canvas_slot', {
            slot: slot + 1,
          })
        ) as any,
      '',
    ],
    [Palette, 'canvas.select_material', () => setDialog(true), ''],
    [RefreshCw, 'canvas.sync', sync, ''],
  ]
  return (
    <Page title="common.canvas" sub="canvas.material_system_overview_and">
      <div class="grid gap-3.5 cc2-xl:grid-cols-[minmax(0,2.55fr)_minmax(320px,1fr)]">
        <Card>
          <div class="grid items-center gap-6 rounded-lg border border-edge bg-field/40 p-4 cc2-sm:p-6 cc2-md:grid-cols-[1.25fr_1fr]">
            <div class="flex h-48 items-end justify-center gap-1.5 border-b-8 border-edge pb-9 cc2-sm:gap-3">
              {slots.map(s => (
                <div key={s.i} class="relative flex h-40 w-14 items-center justify-center cc2-sm:w-20">
                  <svg viewBox="0 0 100 160" class="h-36 w-full" aria-hidden="true">
                    <ellipse cx="50" cy="150" rx="36" ry="6" fill="currentColor" opacity="0.12" />
                    <ellipse cx="64" cy="76" rx="27" ry="67" fill="#334553" stroke="#71828e" stroke-width="2" />
                    <path d="M35 20H64C88 20 88 132 64 132H35Z" fill={s.colour} />
                    {[36, 48, 60, 72, 84, 96, 108, 120].map(y => (
                      <path key={y} d={`M36 ${y}H70`} stroke="#000" stroke-opacity="0.16" stroke-width="1.5" />
                    ))}
                    <ellipse cx="36" cy="76" rx="27" ry="67" fill="#273b49" stroke="#84939e" stroke-width="2" />
                    <ellipse cx="36" cy="76" rx="21" ry="55" fill={s.colour} />
                    <ellipse cx="36" cy="76" rx="17" ry="44" fill="none" stroke="#fff" stroke-opacity="0.22" />
                    <ellipse cx="36" cy="76" rx="12" ry="30" fill="#334553" stroke="#84939e" stroke-width="2" />
                    <ellipse cx="36" cy="76" rx="5" ry="13" fill="#14232e" />
                    <path
                      d="M25 30C15 49 15 102 25 122"
                      fill="none"
                      stroke="#fff"
                      stroke-opacity="0.3"
                      stroke-width="2"
                    />
                  </svg>
                  <em class="absolute -bottom-7 not-italic text-muted">{s.i + 1}</em>
                </div>
              ))}
            </div>
            <div>
              <Tag tone={ok ? 'ok' : 'warning'} class="mb-4">
                <Pip /> {t(ok ? 'common.connected' : 'canvas.not_detected')}
              </Tag>
              <h3 class="mb-3 text-xl leading-snug">
                {t(ok ? 'canvas.canvas_detected_and_connected' : 'canvas.canvas_control_is_not_exposed_by')}
              </h3>
              <p class="text-[15px] text-muted">
                {t(ok ? 'canvas.live_slot_telemetry_is_supplied_by' : 'canvas.cc2_control_will_enable_material')}
              </p>
            </div>
          </div>
          <div class="mt-3.5 grid grid-cols-2 gap-3 cc2-md:grid-cols-4">
            {slots.map(s => (
              <Card
                key={s.i}
                class={cn(
                  'flex min-h-72 flex-col',
                  ok && 'cursor-pointer',
                  s.i === slot && ok && 'outline outline-2 outline-cyan'
                )}
                onClick={() => ok && canvas.set({ slot: s.i })}
              >
                <h3 class="text-base font-semibold">{tpl('common.slot_n', { n: s.i + 1 })}</h3>
                <div
                  class="mx-auto my-2 mb-4 size-24 rounded-full border-[18px] bg-bg"
                  style={{ borderColor: s.colour, boxShadow: `0 0 18px ${s.colour}66` }}
                />
                <div class="mt-auto">
                  <Row label="canvas.material" value={s.material} />
                  <Row label="canvas.color" value={s.raw || '—'} />
                  {s.held && (
                    <Row
                      label="spools.spool"
                      value={
                        s.spool
                          ? spoolLabel(s.spool)
                          : t(s.held.question ? 'spools.question_tag' : 'spools.not_assigned')
                      }
                    />
                  )}
                  <Row label="canvas.remaining" value={s.remaining} />
                </div>
                {s.held && (
                  <Button
                    class="mt-2 text-xs"
                    variant={s.held.question ? 'primary' : 'ghost'}
                    onClick={e => {
                      e.stopPropagation()
                      openChooser(s.i, s.held?.question ? 'question' : 'manual')
                    }}
                  >
                    {t('spools.choose')}
                  </Button>
                )}
                <Button class="mt-3.5" disabled={!ok} onClick={() => canvas.set({ slot: s.i })}>
                  {ok ? (
                    s.i === slot ? (
                      <>
                        <Check {...I} />
                        {t('canvas.selected')}
                      </>
                    ) : (
                      t('canvas.select_slot')
                    )
                  ) : (
                    <>
                      <Info {...I} />
                      {t('canvas.no_telemetry')}
                    </>
                  )}
                </Button>
              </Card>
            ))}
          </div>
        </Card>
        <div class="grid content-start gap-3.5 cc2-lg:grid-cols-3 cc2-xl:grid-cols-1">
          <Card>
            <CardHead icon="info" title="canvas.canvas_status" />
            <Row label="canvas.telemetry" value={t(model ? 'canvas.available' : 'canvas.unavailable')} />
            <Row label="canvas.native_commands" value={t(ok ? 'canvas.available' : 'canvas.unavailable')} />
            <Row label="canvas.last_check" value={checked || '—'} />
            <Button wide class="mt-3.5" onClick={sync}>
              <RefreshCw {...I} />
              {t('canvas.check_again')}
            </Button>
          </Card>
          <Card>
            <CardHead icon="settings" title={ok ? 'canvas.canvas_controls' : 'canvas.controls_unavailable'} />
            <div class="grid gap-2">
              {ctl.map(([Glyph, label, fn]) => (
                <Button
                  key={label}
                  class="justify-between"
                  disabled={!ok || eject.running || ejectPending}
                  onClick={fn}
                >
                  <span class="flex items-center gap-2">
                    <Glyph {...I} />
                    {t(label)}
                  </span>
                  <small class="text-muted">
                    {ok ? tpl('common.slot_n', { n: slot + 1 }) : t('canvas.unavailable')}
                  </small>
                </Button>
              ))}
            </div>
            <div class="cc2-canvas-eject mt-3 border-t border-edge pt-3">
              <Button
                wide
                disabled={eject.running ? false : !ok || !idle || !eject.available || ejectPending}
                onClick={eject.running ? cancelEject : startEject}
              >
                {t(eject.running ? 'canvas.eject_stop' : 'canvas.eject')}
                {!eject.running && ` (${slot + 1})`}
              </Button>
              <p class="mt-2 text-[13px] text-muted">{t('canvas.eject_hint')}</p>
              <p role="status" class="mt-2 text-[13px]">
                {t(ejectResults[eject.result] || 'canvas.eject_fault')}
                {eject.slot >= 0 &&
                  ` · ${tpl('common.slot_n', { n: eject.slot + 1 })} · ${Math.round(eject.travel)} mm`}
              </p>
            </div>
            <div class="mt-3 border-t border-edge pt-3">
              <Button
                wide
                class="justify-between"
                disabled={!ok || autoRefill === null || refillBusy || eject.running || ejectPending}
                aria-pressed={autoRefill === true}
                onClick={toggleRefill}
              >
                <span class="flex items-center gap-2">
                  <Repeat {...I} />
                  {t('canvas.auto_refill')}
                </span>
                <small class={autoRefill ? 'text-cyan' : 'text-muted'}>
                  {autoRefill === null
                    ? t('canvas.auto_refill_not_reported')
                    : t(autoRefill ? 'canvas.auto_refill_on' : 'canvas.auto_refill_off')}
                </small>
              </Button>
              <p class="mt-2 text-[13px] text-muted">{t('canvas.auto_refill_hint')}</p>
            </div>
          </Card>
          <Card>
            <CardHead icon="light" title="canvas.what_still_works" />
            <div class="grid gap-3.5 text-[13px]">
              {(
                [
                  'canvas.manual_filament_loading',
                  'canvas.printing_with_the_selected_tool',
                  'canvas.automatic_detection_when_supported',
                ] as Key[]
              ).map(s => (
                <div key={s} class="flex gap-2.5">
                  <Check {...I} class="shrink-0 text-cyan" />
                  {t(s)}
                </div>
              ))}
            </div>
          </Card>
        </div>
      </div>
      {dialog && (
        <MaterialDialog
          slot={slot}
          onClose={() => {
            setDialog(false)
            refreshCanvas()
          }}
        />
      )}
    </Page>
  )
}
