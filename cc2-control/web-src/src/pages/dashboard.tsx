import { useRef, useState } from 'preact/hooks'
import { Settings } from 'lucide-preact'
import { PrintTuning } from '@/components/print-tuning'
import { QueueNext } from '@/components/queue-next'
import { Card, CardHead } from '@/components/ui/card'
import { Dialog, DialogTitle } from '@/components/ui/dialog'
import { Select } from '@/components/ui/field'
import { Button } from '@/components/ui/button'
import { Dot, Tag } from '@/components/ui/badge'
import { Icon } from '@/components/icons'
import { CameraCard, Progress, Row } from '@/components/shared'
import { ThermalChart } from '@/components/thermal'
import { control, errText, notify, post } from '@/lib/api'
import {
  calibrationFocus,
  controlFocus,
  QUICK_ASK,
  QUICK_CHOICES,
  quick,
  quickAlwaysAvailable,
  saveQuickActions,
} from '@/lib/quick'
import { type Key, t, tpl } from '@/lib/i18n'
import { cn } from '@/lib/utils'
import { grilleAllowed, grilleBusy } from '@/lib/grille'
import { stateText } from '@/lib/machine'
import { num, duration } from '@/lib/format'
import { findMeasure, plates, refreshPlates } from '@/lib/plates'
import { usePoll } from '@/lib/poll'
import { health, openPage, openSettings, type Page, printer, refreshHealth, refreshPrinter, view, zoffset } from '@/lib/state'
import { JobControls } from '@/pages/job'

const Slot = ({ action, idle, lightOn }: { action: string; idle: boolean; lightOn: boolean }) => {
  const [label, icon] = QUICK_CHOICES[action]
  const link = printer.use()
  const isGrille = action === 'grille:open' || action === 'grille:close'
  const grilleWaiting = grilleBusy.use().busy
  const [pending, setPending] = useState(false)
  const sending = useRef(false)
  const blocked = isGrille ? !grilleAllowed(link) : !idle && !quickAlwaysAvailable(action)
  const isLight = action === 'light:toggle'
  const run = async () => {
    if (blocked || sending.current || (isGrille && grilleBusy.get().busy)) return
    if (action === 'settings:connection') return openSettings('connection')
    if (action.startsWith('control:')) {
      if (action === 'control:heating' && !link.data?.bed_drying?.enabled && !link.data?.bed_drying?.active)
        return openSettings('integrations')
      controlFocus.set(s => ({ target: action.slice(8), revision: s.revision + 1 }))
      return openPage('control')
    }
    if (isGrille) {
      grilleBusy.set({ busy: true })
      sending.current = true
      setPending(true)
      try {
        await post('/api/control', action)
        notify(t(action === 'grille:open' ? 'grille.open_accepted' : 'grille.close_accepted'))
      } catch (e) {
        notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
      } finally {
        grilleBusy.set({ busy: false })
        sending.current = false
        setPending(false)
      }
      return
    }
    if (action.startsWith('calibration:')) {
      calibrationFocus.set(s => ({ target: action.slice(12), revision: s.revision + 1 }))
      return openPage('control')
    }
    if (action.startsWith('page:')) return openPage(action.slice(5) as Page)
    if (isLight) {
      if (await control(lightOn ? 'light:off' : 'light:on')) setTimeout(refreshPrinter, 250)
      return
    }
    return control(action, QUICK_ASK[action] ? t(QUICK_ASK[action]) : '')
  }
  return (
    <Button
      class="min-h-18 flex-col"
      disabled={blocked || pending || (isGrille && grilleWaiting)}
      aria-busy={pending || undefined}
      variant={isLight && lightOn ? 'active' : 'default'}
      aria-pressed={isLight ? lightOn : undefined}
      title={
        blocked
          ? t(isGrille ? 'grille.idle_required' : 'common.available_when_idle')
          : isLight
            ? t(lightOn ? 'control.internal_light_on_press_to_turn' : 'control.internal_light_off_press_to_turn')
            : undefined
      }
      onClick={run}
    >
      <Icon n={icon} class={isLight && !lightOn ? 'text-muted' : 'text-cyan'} />
      {t(label)}
    </Button>
  )
}

const QuickEditor = ({ onClose }: { onClose: () => void }) => {
  const [slots, setSlots] = useState(quick.get().actions)
  const save = async () => {
    try {
      await saveQuickActions(slots)
      notify(t('dashboard.quick_actions_saved'))
      onClose()
    } catch (e) {
      notify(tpl('dashboard.save_failed_error', { error: errText(e) }), 'error')
    }
  }
  return (
    <Dialog onClose={onClose} width={440}>
      <DialogTitle class="mb-1">{t('dashboard.configure_quick_actions')}</DialogTitle>
      <p class="mb-4 text-muted">{t('dashboard.choose_four_protected_actions_or')}</p>
      {slots.map((action, n) => (
        <div key={n} class="my-2.5 grid grid-cols-[70px_1fr] items-center gap-2.5">
          <label for={`quick-slot-${n}`}>{tpl('common.slot_n', { n: n + 1 })}</label>
          <Select
            id={`quick-slot-${n}`}
            value={action}
            onChange={e => {
              const v = e.currentTarget.value
              setSlots(list => list.map((a, i) => (i === n ? v : a)))
            }}
          >
            {Object.entries(QUICK_CHOICES).map(([value, [label]]) => (
              <option key={value} value={value}>
                {t(label)}
              </option>
            ))}
          </Select>
        </div>
      ))}
      <div class="mt-5 flex justify-end gap-2.5">
        <Button onClick={onClose}>{t('common.cancel')}</Button>
        <Button variant="primary" onClick={save}>
          {t('dashboard.save_changes')}
        </Button>
      </div>
    </Dialog>
  )
}

// One bordered tile per fan: speed as a large figure, a bar, and a fan icon that spins faster as speed rises.
const FanTile = ({ label, pct }: { label: Key; pct: number }) => (
  <div class="cc2-temp-tile cc2-fan-tile text-cyan">
    <div class="flex items-center gap-2 text-xs text-muted">
      <Icon
        n="fan"
        class={cn('size-4 text-cyan motion-reduce:animate-none', pct > 0 && 'animate-spin')}
        style={{ animationDuration: `${Math.max(0.4, 3 - pct * 0.026)}s` }}
      />
      {t(label)}
    </div>
    <div class="cc2-temp-tile-value text-fg">
      {pct}
      <small>%</small>
    </div>
    <div class="cc2-temp-tile-bar">
      <i style={{ width: `${Math.max(0, Math.min(100, pct))}%` }} />
    </div>
  </div>
)

export const Dashboard = () => {
  usePoll(refreshPrinter, 1500)
  usePoll(refreshHealth, 5000)
  usePoll(refreshPlates, 30000)
  const { data: d, ok: printerAvailable } = printer.use()
  const h = health.use().data
  const off = zoffset.use().v
  const plateStatus = plates.use()
  const plateLibrary = plateStatus.ok ? plateStatus.data : null
  const mountedPlate = plateLibrary?.available && !plateLibrary.pending
    ? plateLibrary.plates.find(plate => plate.id === plateLibrary.current)
    : undefined
  const mesh = plateLibrary?.mesh_profile
  const meshMeasure = mesh?.startsWith('cc2_') ? findMeasure(plateLibrary, mesh.slice(4)) : null
  const meshLabel = meshMeasure
    ? `${meshMeasure.plate.name} · ${meshMeasure.measure.temp} °C`
    : mesh || t('dashboard.mesh_not_detected')
  const v = view(d)
  const quickActions = quick.use().actions
  const [editing, setEditing] = useState(false)
  const target = (x: any) => (Number(x) > 0 ? ` / ${num(x)}` : '')
  return (
    <div class="cc2-dashboard grid gap-3.5">
      {editing && <QuickEditor onClose={() => setEditing(false)} />}
      <div class="cc2-dashboard-primary grid gap-3.5 cc2-lg:grid-cols-2">
        <div class="cc2-dashboard-camera-column grid content-start gap-3.5 cc2-lg:grid-rows-[1fr_auto]">
          <CameraCard preferences />
          <Card class="cc2-quick-actions">
            <CardHead
              icon="bolt"
              title="dashboard.quick_actions"
              end={
                <Button class="min-h-8 px-2 text-xs text-fg" onClick={() => setEditing(true)}>
                  <Settings size={14} strokeWidth={1} />
                  {t('dashboard.edit')}
                </Button>
              }
            />
            <div class="cc2-quick-grid grid grid-cols-2 gap-2 cc2-sm:grid-cols-4">
              {quickActions.map((action, n) => (
                <Slot key={n} action={action} idle={v.idle} lightOn={v.lightOn} />
              ))}
            </div>
          </Card>
        </div>
        <div class="grid gap-3.5 cc2-lg:grid-rows-[1fr_auto]">
          <Card class="flex flex-col">
            <CardHead icon="file" title="common.current_job" end={stateText(v)} />
            <div
              title={v.rawFilename || undefined}
              class="cc2-job-filename mb-3 mt-1 text-2xl font-semibold leading-snug [overflow-wrap:anywhere]"
            >
              {v.rawFilename || t('common.no_active_file')}
            </div>
            {!v.active && (
              <a href="#files" class="mb-3 -mt-1 w-fit text-[13px] text-cyan underline underline-offset-2">
                {t('common.choose_a_file_to_print')}
              </a>
            )}
            <QueueNext />
            <Progress pct={v.progress} />
            <div class="cc2-job-stats mt-5 grid grid-cols-2 gap-x-2.5 gap-y-4 cc2-sm:grid-cols-4">
              {[
                [v.elapsedText, t('common.elapsed')],
                [v.remainingText, tpl('common.remaining_ends_time', { time: v.finishText })],
                [v.active ? v.layer || '—' : '—', t('common.current_layer')],
                [v.total, t('common.total_layers')],
              ].map(([val, label], i) => (
                <div key={i} class="border-edge cc2-sm:border-r cc2-sm:last:border-0">
                  <strong class="block text-[15px]">{val}</strong>
                  <small class="text-xs text-muted">{label}</small>
                </div>
              ))}
            </div>
            <div class="mt-auto">
              <JobControls v={v} />
            </div>
          </Card>
          <PrintTuning sliders />
        </div>
      </div>

      <div class="grid gap-3.5 cc2-md:grid-cols-2 cc2-lg:grid-cols-4">
        <Card class="flex flex-col cc2-md:col-span-2">
          <CardHead icon="temp" title="dashboard.thermals" end={t('dashboard.live_history_5_minutes')} />
          <div class="grid gap-3.5">
            <div class="grid gap-2.5 cc2-sm:grid-cols-3">
              {(
                [
                  ['red', 'common.nozzle', d?.extruder?.temperature, d?.extruder?.target, 300],
                  ['blue', 'common.heated_bed', d?.heater_bed?.temperature, d?.heater_bed?.target, 120],
                  ['amber', 'dashboard.chamber', d?.chamber?.temperature, 0, 65],
                ] as const
              ).map(([dot, label, val, goal, max]) => (
                <div key={label} class="cc2-temp-tile" style={{ color: `var(--${dot})` }}>
                  <div class="flex items-center gap-2 text-xs text-muted">
                    <Dot c={dot} />
                    {t(label)}
                  </div>
                  <div class="cc2-temp-tile-value text-fg">
                    {num(val)}
                    <small>{target(goal)} °C</small>
                  </div>
                  <div class="cc2-temp-tile-bar">
                    <i style={{ width: `${Math.max(0, Math.min(100, (Number(val) / max) * 100)) || 0}%` }} />
                  </div>
                </div>
              ))}
            </div>
            <ThermalChart />
          </div>
          <dl class="mt-auto grid gap-2 pt-3 cc2-sm:grid-cols-2" data-heater-status>
            {([
              ['common.nozzle', d?.extruder?.target],
              ['common.heated_bed', d?.heater_bed?.target],
            ] as const).map(([label, goal]) => {
              const known = printerAvailable && d?.connected === true && typeof goal === 'number' && Number.isFinite(goal)
              return (
                <div key={label} class="flex min-w-0 items-center justify-between gap-2 rounded-md border border-edge px-2 py-2">
                  <dt class="text-sm text-muted">{t(label)}</dt>
                  <dd title={tpl('control.name_target', { name: t(label) })}>
                    <Tag>{t(!known ? 'state.unknown' : goal > 0 ? 'control.on' : 'control.off')}</Tag>
                  </dd>
                </div>
              )
            })}
          </dl>
        </Card>
        <Card class="cc2-dashboard-fans flex flex-col">
          <CardHead icon="fan" title="common.fans" />
          <div class="mt-4 flex flex-1 flex-col gap-2.5 cc2-lg:mt-0 cc2-lg:gap-4.5">
            <FanTile label="common.part_fan" pct={v.fan('part')} />
            <FanTile label="common.aux_fan" pct={v.fan('aux')} />
            <FanTile label="dashboard.chamber" pct={v.fan('box')} />
          </div>
        </Card>
        <Card class="flex min-w-0 flex-col" data-first-layer>
          <CardHead icon="z" title="dashboard.first_layer" />
          <dl class="grid flex-1 grid-rows-2 gap-2 text-center">
            <div class="flex min-w-0 flex-col justify-center rounded-md border border-edge bg-field px-2 py-3" title={t('dashboard.z_offset_page_only')}>
              <dt class="text-base font-semibold text-fg">{t('dashboard.effective_z_offset')}</dt>
              <dd class="mt-2 flex flex-col items-center gap-2">
                <span class="text-3xl font-semibold tabular-nums">
                  {off === null ? '—' : (off > 0 ? '+' : '') + off.toFixed(3)} <small class="text-sm text-muted">mm</small>
                </span>
                <Tag tone="warning">{t('common.session_only')}</Tag>
              </dd>
            </div>
            <div class="flex min-w-0 flex-col justify-center rounded-md border border-edge bg-field px-2 py-3">
              <dt class="text-base font-semibold text-fg">{t('bed.active_mesh')}</dt>
              <dd class="mt-2 truncate text-xl font-semibold" title={meshLabel}>{meshLabel}</dd>
              <dd class="mt-2 truncate text-base font-semibold text-fg" title={mountedPlate?.name}>
                {mountedPlate ? tpl('plates.side_n', { side: mountedPlate.side }) : t('dashboard.plate_side_unknown')}
              </dd>
            </div>
          </dl>
        </Card>
      </div>

      <div class="cc2-dashboard-bottom grid gap-3.5">
        <div class="grid gap-3.5 cc2-md:grid-cols-2">
          <Card class="flex flex-col">
            <CardHead icon="target" title="common.position" />
            {(['x', 'y', 'z'] as const).map(a => (
              <Row key={a} text={a.toUpperCase()} value={v.pos(a)} />
            ))}
            <div class="mt-auto pt-3">
              <Tag tone={['x', 'y', 'z'].every(a => v.homed.includes(a)) ? 'ok' : 'warning'}>
                {v.homed ? `${v.homed.toUpperCase()} ${t('common.homed')}` : t('common.not_homed')}
              </Tag>
            </div>
          </Card>
          <Card>
            <CardHead icon="monitor" title="dashboard.system" />
            <Row
              label="dashboard.service"
              value={
                h ? (
                  <>
                    <Dot /> {t('dashboard.online')}
                  </>
                ) : (
                  t('common.connecting')
                )
              }
            />
            <Row label="dashboard.system_uptime" value={h ? duration(h.uptime_seconds) : '—'} />
            <Row label="dashboard.cpu_load" value={h ? String(h.loadavg).split(/\s+/).slice(0, 3).join(' ') : '—'} />
            <Row label="dashboard.available_memory" value={h ? `${(h.mem_available_kb / 1024).toFixed(1)} MiB` : '—'} />
          </Card>
        </div>
      </div>
    </div>
  )
}
