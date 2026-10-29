import { useState } from 'preact/hooks'
import { ArrowLeftRight, EthernetPort, RefreshCw, Wifi } from 'lucide-preact'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Pip, Tag } from '@/components/ui/badge'
import { Switch } from '@/components/ui/field'
import { request } from '@/lib/api'
import { type Key, t } from '@/lib/i18n'
import { usePoll } from '@/lib/poll'
import { ls, store } from '@/lib/store'
import { cn } from '@/lib/utils'

type Interface = {
  present: boolean
  enabled: boolean
  carrier: boolean | null
  address: string | null
  address_mode: string
  link_mbps: number | null
}
type Snapshot = {
  schema: number
  wifi: Interface
  ethernet: Interface
  wireless: {
    control_available: boolean
    state: string
    ssid: string
    security: string
    frequency_mhz: number | null
    signal_dbm: number | null
  }
  route: { selected: string }
}
type Envelope = { available: boolean; fresh: boolean; age_ms: number | null; snapshot: Snapshot | null }
const status = store({ data: null as Envelope | null, ok: false, received: 0 })
let pending: Promise<void> | null = null
const refresh = () => {
  if (pending) return pending
  pending = (async () => {
    try {
      const data = (await request('/api/network')) as Envelope
      if (data.snapshot && data.snapshot.schema !== 1) throw new Error('Unsupported network status schema')
      status.set({ data, ok: true, received: performance.now() })
    } catch {
      status.set({ ok: false })
    } finally {
      pending = null
    }
  })()
  return pending
}
const glyph = { size: 16, strokeWidth: 1 }
const Value = ({ label, value }: { label: Key; value: string | number | null | undefined }) => (
  <div class="min-w-0">
    <dt class="text-xs text-muted">{t(label)}</dt>
    <dd class="mt-1 font-semibold [overflow-wrap:anywhere]">{value ?? '—'}</dd>
  </div>
)

export const Network = () => {
  const { data, ok, received } = status.use()
  usePoll(refresh, data?.available && ok ? 5000 : 1000)
  const fresh = Boolean(ok && data?.fresh && (data.age_ms ?? 8000) + performance.now() - received < 8000)
  const d = fresh && data?.available ? data.snapshot : null
  const [ethernet, setEthernet] = useState(ls.get('cc2-network-show-ethernet') === '1')
  const [busy, setBusy] = useState(false)
  const update = async () => {
    setBusy(true)
    try {
      await refresh()
    } finally {
      setBusy(false)
    }
  }
  const route = d?.route.selected
  const connected = Boolean(d?.wifi.enabled && d.wifi.address && d.wifi.carrier && d.wireless.state === 'COMPLETED')
  const modeLabel =
    route === 'wifi' ? 'network.wifi' : route === 'ethernet' ? 'network.ethernet' : 'network.unavailable'
  return (
    <div class="cc2-network grid gap-3.5">
      <div class="flex flex-wrap items-center justify-between gap-2">
        <p class="text-xs text-amber">{t('network.readonly')}</p>
        <Button disabled={busy} onClick={update}>
          <RefreshCw {...glyph} />
          {t('network.refresh')}
        </Button>
      </div>
      {!fresh && (
        <p role="status" class="text-amber">
          {t(ok ? 'network.reading' : 'network.unavailable')}
        </p>
      )}
      <Card>
        <div class="relative flex min-h-12 items-center gap-3 pr-14">
          <EthernetPort size={22} strokeWidth={1} class="shrink-0 text-cyan" aria-hidden="true" />
          <div class="min-w-0">
            <h3 class="text-base font-semibold">{t('network.support')}</h3>
            <p class="text-xs text-muted">{t('network.support_help')}</p>
          </div>
          <span class="absolute right-0 top-1/2 -translate-y-1/2">
            <Switch
              on={ethernet}
              label={t('network.show_options')}
              onClick={() => {
                const next = !ethernet
                ls.set('cc2-network-show-ethernet', next ? '1' : '0')
                setEthernet(next)
              }}
            />
          </span>
        </div>
      </Card>
      <div class={cn('grid gap-3.5', ethernet && 'cc2-md:grid-cols-2')}>
        <Card class="flex flex-col">
          <CardHead
            icon="wifi"
            title="network.wifi"
            end={
              <Tag tone={connected ? 'ok' : 'default'}>
                <Pip hollow={!connected} />
                {t(
                  !d
                    ? 'network.unavailable'
                    : connected
                      ? 'network.connected'
                      : d.wifi.enabled
                        ? 'network.active'
                        : 'network.inactive'
                )}
              </Tag>
            }
          />
          <h3 class="text-base font-semibold [overflow-wrap:anywhere]">{d?.wireless.ssid || t('network.wifi')}</h3>
          <p class="mt-1 min-h-5 text-xs text-muted [overflow-wrap:anywhere]">{d?.wireless.security || '—'}</p>
          <dl class="my-3 grid grid-cols-2 gap-3 cc2-sm:grid-cols-3">
            <Value label="network.ip" value={d?.wifi.address} />
            <Value
              label="network.signal"
              value={d?.wireless.signal_dbm != null ? `${d.wireless.signal_dbm} dBm` : null}
            />
            <Value
              label="network.frequency"
              value={d?.wireless.frequency_mhz ? `${d.wireless.frequency_mhz} MHz` : null}
            />
          </dl>
          {d && !d.wireless.control_available && <p class="mb-2 text-xs text-amber">{t('network.details_missing')}</p>}
          <div class="mt-auto flex flex-wrap items-center gap-2">
            <Button disabled>
              <Wifi {...glyph} />
              {t('network.change_network')}
            </Button>
            <Button disabled>{t('network.configure_ip')}</Button>
            {route === 'wifi' && <span class="ml-auto text-xs text-cyan">{t('network.selected')}</span>}
          </div>
        </Card>
        {ethernet && (
          <Card class="flex flex-col">
            <CardHead
              icon="ethernet"
              title="network.ethernet"
              end={
                <Tag tone={d?.ethernet.carrier ? 'ok' : 'default'}>
                  <Pip hollow={!d?.ethernet.carrier} />
                  {t(d?.ethernet.carrier ? 'network.link' : 'network.inactive')}
                </Tag>
              }
            />
            <h3 class="text-base font-semibold">{t('network.ethernet')}</h3>
            <p class="mt-1 min-h-5 text-xs text-muted">
              {t(d?.ethernet.present ? 'network.interface_present' : 'network.missing_interface')}
            </p>
            <dl class="my-3 grid grid-cols-2 gap-3 cc2-sm:grid-cols-3">
              <Value label="network.ip" value={d?.ethernet.address} />
              <Value label="network.assignment" value={t('network.unknown')} />
              <Value label="network.speed" value={d?.ethernet.link_mbps ? `${d.ethernet.link_mbps} Mbit/s` : null} />
            </dl>
            <div class="mt-auto flex flex-wrap items-center gap-2">
              <Button disabled>{t('network.configure_ip')}</Button>
              {route === 'ethernet' && <span class="ml-auto text-xs text-cyan">{t('network.selected')}</span>}
            </div>
          </Card>
        )}
      </div>
      {ethernet && (
        <Card>
          <CardHead icon="link" title="network.modes" />
          <div class="grid grid-cols-1 gap-2 cc2-sm:grid-cols-3" role="radiogroup" aria-label={t('network.modes')}>
            {(
              [
                ['wifi', 'network.wifi', Wifi],
                ['ethernet', 'network.ethernet', EthernetPort],
                ['automatic', 'network.automatic', ArrowLeftRight],
              ] as const
            ).map(([mode, label, Icon]) => (
              <Button
                key={mode}
                disabled
                role="radio"
                aria-checked={mode === route}
                variant={mode === route ? 'active' : 'default'}
                class="min-w-0 flex-col cc2-sm:flex-row"
              >
                <Icon {...glyph} />
                {t(label)}
              </Button>
            ))}
          </div>
          <p class="mt-2 text-xs text-muted">
            {t('network.current_route')}: <strong>{t(modeLabel)}</strong>
          </p>
        </Card>
      )}
    </div>
  )
}
