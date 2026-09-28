import { Card, CardHead } from '@/components/ui/card'
import { ArrowUpRight } from 'lucide-preact'
import { Button } from '@/components/ui/button'
import { Dot, Tag } from '@/components/ui/badge'
import { Icon } from '@/components/icons'
import { CameraCard, FanBar, Progress, Reading } from '@/components/shared'
import { ThermalChart } from '@/components/thermal'
import { control, } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { num, duration } from '@/lib/format'
import { health, openPage, printer, view, zoffset } from '@/lib/state'

const quick: [string, string, string, string][] = [
  ['home:ALL', 'home', 'Home All', 'Home all axes?'], ['system:heaters_off', 'temp', 'All Heaters Off', 'Turn all heaters off?'],
  ['system:fans_off', 'fan', 'Fans Off', 'Turn all fans off?'], ['system:motors_off', 'motors', 'Motors Off', 'Disable all motors?'],
]

const I = { size: 16, strokeWidth: 1 }

const Link = ({ to, label }: { to: Parameters<typeof openPage>[0]; label: string }) => (
  <div class="mt-auto pt-2.5"><Button wide class="text-xs" onClick={() => openPage(to)}>{t(label)}<ArrowUpRight {...I} /></Button></div>
)

export const Dashboard = () => {
  const d = printer.use().data
  const h = health.use().data
  const off = zoffset.use().v
  const v = view(d)
  const c = d?.chamber, layerText = `${t('Layer')} ${v.layer || '—'} / ${v.total}`
  return (
    <div class="grid gap-3.5">
      <div class="grid gap-3.5 lg:grid-cols-2">
        <Card class="flex flex-col"><CardHead icon="camera" title="Live Camera" end={t('Live')} /><CameraCard /></Card>
        <Card class="flex flex-col">
          <CardHead icon="file" title="Current Job" end={v.active ? t(v.state) : t('No active file')} />
          <div class="mt-1 text-2xl font-semibold leading-snug [overflow-wrap:anywhere]">{v.rawFilename === 'No active file' ? t(v.rawFilename) : v.rawFilename}</div>
          <div class="my-2 flex justify-between text-xs text-muted"><span>{layerText}<br />{v.active ? t(v.state) : t('No active file')}</span></div>
          <Progress pct={v.progress} />
          <div class="mt-5 grid grid-cols-4 gap-2.5 text-center sm:text-left">
            {[[v.elapsedText, t('Elapsed')], [v.remainingText, tpl('Remaining · ends {time}', { time: v.finishText })], [v.active ? v.layer || '—' : '—', t('Current layer')], [v.total, t('Total layers')]].map(([val, label], i) => (
              <div key={i} class="border-r border-edge last:border-0"><strong class="block text-[15px]">{val}</strong><small class="text-[11px] text-muted">{label}</small></div>
            ))}
          </div>
          <Link to="job" label="Open current job" />
        </Card>
        <Card>
          <CardHead icon="temp" title="Thermals" end={t('Live history · 5 minutes')} />
          <div class="grid items-center gap-4 sm:grid-cols-[1.6fr_1fr]">
            <ThermalChart />
            <div class="grid gap-3 text-xs">
              {([['red', 'Nozzle', d?.extruder?.temperature], ['blue', 'Bed', d?.heater_bed?.temperature], ['amber', 'Chamber', c?.temperature]] as const).map(([dot, label, val]) => (
                <div key={label} class="flex items-center gap-2 whitespace-nowrap"><Dot c={dot} />{t(label)}<b class="ml-auto font-medium">{num(val)} °C</b></div>
              ))}
            </div>
          </div>
        </Card>
        <Card class="flex flex-col">
          <CardHead icon="bolt" title="Quick Actions" />
          <div class="grid grid-cols-2 gap-2.5 sm:grid-cols-4">
            {quick.map(([action, icon, label, ask]) => (
              <Button key={action} class="min-h-18 flex-col" disabled={action !== 'system:heaters_off' && !v.idle} onClick={() => control(action, t(ask))}><Icon n={icon} class="size-6 text-cyan" />{t(label)}</Button>
            ))}
          </div>
          <div class="mt-3 rounded-md border border-edge p-3 text-xs"><strong class="mb-1 block text-[13px]"><Dot c={v.idle ? 'green' : 'amber'} /> {t(v.state)}</strong><span class="text-muted">{v.homed ? `${t('Homed axes')}: ${v.homed.toUpperCase()}` : t('Axes not homed')}</span></div>
        </Card>
      </div>

      <div class="grid grid-cols-2 gap-3 lg:grid-cols-3 xl:grid-cols-5">
        <Card class="flex flex-col"><CardHead icon="temp" title="Temperatures" />
          <div class="grid gap-3"><Reading dot="red" label="Nozzle" value={`${num(d?.extruder?.temperature)} °C`} target={`/ ${num(d?.extruder?.target)}`} /><Reading dot="blue" label="Heated Bed" value={`${num(d?.heater_bed?.temperature)} °C`} target={`/ ${num(d?.heater_bed?.target)}`} /><Reading dot="amber" label="Chamber" value={`${num(c?.temperature)} °C`} target="—" /></div>
          <Link to="control" label="Temperature controls" /></Card>
        <Card class="flex flex-col"><CardHead icon="fan" title="Fans" /><FanBar label="Part Fan" pct={v.fan('part')} /><FanBar label="Aux Fan" pct={v.fan('aux')} /><FanBar label="Chamber" pct={v.fan('box')} /><Link to="control" label="Fan controls" /></Card>
        <Card class="flex flex-col"><CardHead icon="z" title="Live Z Offset" /><small class="text-muted">{t('Session adjustment')}</small>
          <div class="my-2 text-3xl">{(off > 0 ? '+' : '') + off.toFixed(2)} <small class="text-sm text-muted">mm</small></div><Tag tone="warning">{t('Session only')}</Tag><Link to="control" label="Adjust in Control" /></Card>
        <Card class="flex flex-col"><CardHead icon="target" title="Position" />
          <div class="grid gap-2">{(['x', 'y', 'z'] as const).map(a => <Reading key={a} label={a.toUpperCase()} value={v.pos(a)} />)}</div>
          <div class="mt-auto pt-2"><Tag tone={['x', 'y', 'z'].every(a => v.homed.includes(a)) ? 'ok' : 'warning'}>{v.homed ? `${v.homed.toUpperCase()} ${t('homed')}` : t('Not homed')}</Tag></div></Card>
        <Card class="flex flex-col"><CardHead icon="target" title="Bed Calibration" /><p class="text-[13px] text-muted">{t('View the bed surface and measure tilt at the four screws.')}</p><Link to="bed" label="Open Bed Calibration" /></Card>
      </div>

      <div class="grid gap-3.5 lg:grid-cols-[1.9fr_1fr]">
        <Card><CardHead icon="monitor" title="System" />
          <div class="grid grid-cols-2 gap-4 md:grid-cols-4">
            {[[t('Service'), h ? <><Dot /> {t('Online')}</> : t('Connecting…'), 'protected-control'], [t('System Uptime'), h ? duration(h.uptime_seconds) : '—', 'TinaLinux'],
              [t('CPU Load'), h ? String(h.loadavg).split(/\s+/).slice(0, 3).join(' ') : '—', '1 / 5 / 15 min'], [t('Available Memory'), h ? `${(h.mem_available_kb / 1024).toFixed(1)} MiB` : '—', '']].map(([label, val, sub], i) => (
              <div key={i}><small class="block text-muted">{label}</small><strong class="block text-sm font-semibold">{val}</strong><small class="block text-muted">{sub}</small></div>
            ))}
          </div>
        </Card>
        <Card><CardHead icon="message" title="Messages" />
          <div class="flex items-center justify-between"><div><strong class="block text-2xl">{d ? d.messages || 0 : '—'}</strong><small class="text-[11px] text-muted">{t('MQTT messages received')}</small></div><Button class="text-xs" onClick={() => openPage('console')}>{t('View Console')}<ArrowUpRight {...I} /></Button></div></Card>
      </div>
    </div>
  )
}
