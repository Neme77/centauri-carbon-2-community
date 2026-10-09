import { useState } from 'preact/hooks'
import { Settings } from 'lucide-preact'
import { PrintTuning } from '@/components/print-tuning'
import { Card, CardHead } from '@/components/ui/card'
import { Dialog } from '@/components/ui/dialog'
import { Select } from '@/components/ui/field'
import { Button } from '@/components/ui/button'
import { Dot, Tag } from '@/components/ui/badge'
import { Icon } from '@/components/icons'
import { CameraCard, FanBar, Progress, Row } from '@/components/shared'
import { ThermalChart } from '@/components/thermal'
import { control, errText, notify } from '@/lib/api'
import { calibrationFocus, QUICK_ASK, QUICK_CHOICES, quick, quickAlwaysAvailable, saveQuickActions } from '@/lib/quick'
import { t, tpl } from '@/lib/i18n'
import { stateText } from '@/lib/machine'
import { num, duration } from '@/lib/format'
import { usePoll } from '@/lib/poll'
import { health, openPage, type Page, printer, refreshHealth, refreshPrinter, view, zoffset } from '@/lib/state'
import { JobControls } from '@/pages/job'

const Slot = ({ action, idle, lightOn }: { action: string; idle: boolean; lightOn: boolean }) => {
  const [label, icon] = QUICK_CHOICES[action]
  const blocked = !idle && !quickAlwaysAvailable(action)
  const isLight = action === 'light:toggle'
  const run = async () => {
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
      disabled={blocked}
      variant={isLight && lightOn ? 'active' : 'default'}
      aria-pressed={isLight ? lightOn : undefined}
      title={
        blocked
          ? t('common.available_when_idle')
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
      <h2 class="mb-1 text-xl font-semibold">{t('dashboard.configure_quick_actions')}</h2>
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

export const Dashboard = () => {
  usePoll(refreshPrinter, 1500)
  usePoll(refreshHealth, 5000)
  const d = printer.use().data
  const h = health.use().data
  const off = zoffset.use().v
  const v = view(d)
  const quickActions = quick.use().actions
  const [editing, setEditing] = useState(false)
  const target = (x: any) => (Number(x) > 0 ? ` / ${num(x)}` : '')
  return (
    <div class="cc2-dashboard grid gap-3.5">
      {editing && <QuickEditor onClose={() => setEditing(false)} />}
      <div class="cc2-dashboard-primary grid gap-3.5 cc2-lg:grid-cols-2">
        <div class="cc2-dashboard-camera-column grid content-start gap-3.5 cc2-lg:grid-rows-[1fr_auto] cc2-lg:[&_.cc2-camera-frame]:grow cc2-lg:[&_.cc2-camera-frame]:shrink-0 cc2-lg:[&_.cc2-camera-frame]:basis-auto">
          <CameraCard />
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
            <div class="grid grid-cols-2 gap-2.5 cc2-sm:grid-cols-4">
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
          <PrintTuning />
        </div>
      </div>

      <div class="grid gap-3.5 cc2-md:grid-cols-2 cc2-lg:grid-cols-4">
        <Card class="cc2-md:col-span-2">
          <CardHead icon="temp" title="dashboard.thermals" end={t('dashboard.live_history_5_minutes')} />
          <div class="grid items-center gap-4 cc2-sm:grid-cols-[1.6fr_1fr]">
            <ThermalChart />
            <div class="grid gap-3 text-xs">
              {(
                [
                  ['red', 'common.nozzle', d?.extruder?.temperature, d?.extruder?.target],
                  ['blue', 'common.heated_bed', d?.heater_bed?.temperature, d?.heater_bed?.target],
                  ['amber', 'dashboard.chamber', d?.chamber?.temperature, 0],
                ] as const
              ).map(([dot, label, val, goal]) => (
                <div key={label} class="flex items-center gap-2 whitespace-nowrap">
                  <Dot c={dot} />
                  {t(label)}
                  <b class="ml-auto font-medium">
                    {num(val)}
                    {target(goal)} °C
                  </b>
                </div>
              ))}
            </div>
          </div>
        </Card>
        <Card>
          <CardHead icon="fan" title="common.fans" />
          <FanBar label="common.part_fan" pct={v.fan('part')} />
          <FanBar label="common.aux_fan" pct={v.fan('aux')} />
          <FanBar label="dashboard.chamber" pct={v.fan('box')} />
        </Card>
        <Card class="flex flex-col">
          <CardHead icon="z" title="dashboard.effective_z_offset" />
          <div class="my-2 text-3xl">
            {off === null ? '—' : (off > 0 ? '+' : '') + off.toFixed(2)} <small class="text-sm text-muted">mm</small>
          </div>
          <div class="mt-auto grid justify-items-start gap-1.5">
            <small class="text-muted">{t('dashboard.z_offset_page_only')}</small>
            <Tag tone="warning">{t('common.session_only')}</Tag>
          </div>
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
