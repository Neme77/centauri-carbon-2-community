import { useEffect, useState } from 'preact/hooks'
import { ask } from '@/lib/confirm'
import { ArrowBigDown, ArrowBigUp, Check, Info, Palette, RefreshCw } from 'lucide-preact'
import { cn } from '@/lib/utils'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Pip, Tag } from '@/components/ui/badge'
import { Select } from '@/components/ui/field'
import { Dialog } from '@/components/ui/dialog'
import { Row } from '@/components/shared'
import { control, errText, notify, post } from '@/lib/api'
import { type Key, t, tpl } from '@/lib/i18n'
import { poll } from '@/lib/poll'
import { canvas, canvasColour, canvasHex, refreshCanvas } from '@/lib/canvas'
import { presets } from '@/lib/state'

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
          <div class="grid grid-cols-5 gap-2 sm:grid-cols-8">
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
  const { model, slot, optimistic, checked } = canvas.use()
  const [dialog, setDialog] = useState(false)
  // Refresh while the page is open, except behind the material dialog.
  useEffect(() => (dialog ? undefined : poll(refreshCanvas, 2000)), [dialog])
  const ok = Boolean(model?.connected)
  const sync = async () => {
    try {
      await post('/api/canvas/refresh')
      setTimeout(refreshCanvas, 600)
    } catch (e) {
      notify(errText(e), 'error')
    }
  }
  const slots = [0, 1, 2, 3].map(i => {
    const tray = model?.trays[i],
      tel = tray?.filament_color
    const saved = optimistic[i]
    const pend = saved && Date.now() < saved.until && canvasHex(tel, i) !== saved.colour ? saved : undefined // drop it once expired or confirmed by telemetry
    const raw = pend ? pend.colour : tel
    return {
      i,
      colour: canvasColour(raw, i),
      raw,
      material: pend ? pend.material : (tray && (tray.filament_name || tray.filament_type)) || '—',
      remaining: tray && tray.remaining_percent !== undefined ? `${tray.remaining_percent}%` : '—',
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
      <div class="grid gap-3.5 xl:grid-cols-[minmax(0,2.55fr)_minmax(320px,1fr)]">
        <Card>
          <div class="grid items-center gap-6 rounded-lg border border-edge bg-field/40 p-4 sm:p-6 md:grid-cols-[1.25fr_1fr]">
            <div class="flex h-48 items-end justify-center gap-1.5 border-b-8 border-edge pb-9 sm:gap-3">
              {slots.map(s => (
                <div key={s.i} class="relative flex h-40 w-14 items-center justify-center sm:w-20">
                  <i
                    class="block h-36 w-9 rounded-[45%] border border-muted sm:w-12"
                    style={{ background: `repeating-linear-gradient(90deg,${s.colour} 0 3px,#132a38 4px 6px)` }}
                  />
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
          <div class="mt-3.5 grid grid-cols-2 gap-3 md:grid-cols-4">
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
                <h3 class="text-base font-semibold">Slot {s.i + 1}</h3>
                <div
                  class="mx-auto my-2 mb-4 size-24 rounded-full border-[18px] bg-bg"
                  style={{ borderColor: s.colour, boxShadow: `0 0 18px ${s.colour}66` }}
                />
                <div class="mt-auto">
                  <Row label="canvas.material" value={s.material} />
                  <Row label="canvas.color" value={s.raw || '—'} />
                  <Row label="canvas.remaining" value={s.remaining} />
                </div>
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
        <div class="grid content-start gap-3.5 lg:grid-cols-3 xl:grid-cols-1">
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
                <Button key={label} class="justify-between" disabled={!ok} onClick={fn}>
                  <span class="flex items-center gap-2">
                    <Glyph {...I} />
                    {t(label)}
                  </span>
                  <small class="text-muted">{ok ? `Slot ${slot + 1}` : t('canvas.unavailable')}</small>
                </Button>
              ))}
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
