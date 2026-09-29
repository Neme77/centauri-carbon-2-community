import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Dot, Tag } from '@/components/ui/badge'
import { Icon } from '@/components/icons'
import { CameraCard, FanBar, Progress, Row } from '@/components/shared'
import { ThermalChart } from '@/components/thermal'
import { control } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { num, duration } from '@/lib/format'
import { health, printer, view, zoffset } from '@/lib/state'
import { JobControls } from '@/pages/job'

const quick: [string, string, string, string][] = [
  ['home:ALL', 'home', 'Home All', 'Home all axes?'],
  ['system:heaters_off', 'temp', 'All Heaters Off', 'Turn all heaters off?'],
  ['system:fans_off', 'fan', 'Fans Off', 'Turn all fans off?'],
  ['system:motors_off', 'motors', 'Motors Off', 'Disable all motors?'],
]

export const Dashboard = () => {
  const d = printer.use().data
  const h = health.use().data
  const off = zoffset.use().v
  const v = view(d)
  const target = (x: any) => (Number(x) > 0 ? ` / ${num(x)}` : '')
  return (
    <div class="grid gap-3.5">
      <div class="grid gap-3.5 lg:grid-cols-2">
        <CameraCard />
        <div class="grid gap-3.5 lg:grid-rows-[1fr_auto]">
          <Card class="flex flex-col">
            <CardHead icon="file" title="Current Job" end={t(v.state)} />
            <div class="mb-3 mt-1 text-2xl font-semibold leading-snug [overflow-wrap:anywhere]">
              {v.rawFilename === 'No active file' ? t(v.rawFilename) : v.rawFilename}
            </div>
            {!v.active && (
              <a href="#files" class="mb-3 -mt-1 w-fit text-[13px] text-cyan underline underline-offset-2">
                {t('Choose a file to print')}
              </a>
            )}
            <Progress pct={v.progress} />
            <div class="mt-5 grid grid-cols-2 gap-x-2.5 gap-y-4 sm:grid-cols-4">
              {[
                [v.elapsedText, t('Elapsed')],
                [v.remainingText, tpl('Remaining · ends {time}', { time: v.finishText })],
                [v.active ? v.layer || '—' : '—', t('Current layer')],
                [v.total, t('Total layers')],
              ].map(([val, label], i) => (
                <div key={i} class="border-edge sm:border-r sm:last:border-0">
                  <strong class="block text-[15px]">{val}</strong>
                  <small class="text-[11px] text-muted">{label}</small>
                </div>
              ))}
            </div>
            <div class="mt-auto">
              <JobControls v={v} />
            </div>
          </Card>
          <Card>
            <CardHead icon="bolt" title="Quick Actions" />
            <div class="grid grid-cols-2 gap-2.5 sm:grid-cols-4">
              {quick.map(([action, icon, label, ask]) => (
                <Button
                  key={action}
                  class="min-h-18 flex-col"
                  disabled={action !== 'system:heaters_off' && !v.idle}
                  title={action !== 'system:heaters_off' && !v.idle ? t('Available when idle') : undefined}
                  onClick={() => control(action, t(ask))}
                >
                  <Icon n={icon} class="size-6 text-cyan" />
                  {t(label)}
                </Button>
              ))}
            </div>
          </Card>
        </div>
      </div>

      <div class="grid gap-3.5 md:grid-cols-2 lg:grid-cols-4">
        <Card class="md:col-span-2">
          <CardHead icon="temp" title="Thermals" end={t('Live history · 5 minutes')} />
          <div class="grid items-center gap-4 sm:grid-cols-[1.6fr_1fr]">
            <ThermalChart />
            <div class="grid gap-3 text-xs">
              {(
                [
                  ['red', 'Nozzle', d?.extruder?.temperature, d?.extruder?.target],
                  ['blue', 'Heated Bed', d?.heater_bed?.temperature, d?.heater_bed?.target],
                  ['amber', 'Chamber', d?.chamber?.temperature, 0],
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
          <CardHead icon="fan" title="Fans" />
          <FanBar label="Part Fan" pct={v.fan('part')} />
          <FanBar label="Aux Fan" pct={v.fan('aux')} />
          <FanBar label="Chamber" pct={v.fan('box')} />
        </Card>
        <Card class="flex flex-col">
          <CardHead icon="z" title="Live Z Offset" />
          <div class="my-2 text-3xl">
            {(off > 0 ? '+' : '') + off.toFixed(2)} <small class="text-sm text-muted">mm</small>
          </div>
          <div class="mt-auto">
            <Tag tone="warning">{t('Session only')}</Tag>
          </div>
        </Card>
      </div>

      <div class="grid gap-3.5 md:grid-cols-2">
        <Card class="flex flex-col">
          <CardHead icon="target" title="Position" />
          {(['x', 'y', 'z'] as const).map(a => (
            <Row key={a} label={a.toUpperCase()} value={v.pos(a)} />
          ))}
          <div class="mt-auto pt-3">
            <Tag tone={['x', 'y', 'z'].every(a => v.homed.includes(a)) ? 'ok' : 'warning'}>
              {v.homed ? `${v.homed.toUpperCase()} ${t('homed')}` : t('Not homed')}
            </Tag>
          </div>
        </Card>
        <Card>
          <CardHead icon="monitor" title="System" />
          <Row
            label="Service"
            value={
              h ? (
                <>
                  <Dot /> {t('Online')}
                </>
              ) : (
                t('Connecting…')
              )
            }
          />
          <Row label="System Uptime" value={h ? duration(h.uptime_seconds) : '—'} />
          <Row label="CPU Load" value={h ? String(h.loadavg).split(/\s+/).slice(0, 3).join(' ') : '—'} />
          <Row label="Available Memory" value={h ? `${(h.mem_available_kb / 1024).toFixed(1)} MiB` : '—'} />
        </Card>
      </div>
    </div>
  )
}
