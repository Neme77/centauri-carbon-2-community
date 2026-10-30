import { useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Dialog, DialogTitle } from '@/components/ui/dialog'
import { Input, PasswordInput, Select, Switch } from '@/components/ui/field'
import { Pip, Tag } from '@/components/ui/badge'
import { request, errText } from '@/lib/api'
import { ask } from '@/lib/confirm'
import { type Key, t } from '@/lib/i18n'
import { usePoll } from '@/lib/poll'
import { store } from '@/lib/store'

type Link = 'wifi' | 'ethernet'
type IP = {
  enabled: boolean
  method: 'dhcp' | 'static'
  ip: string
  prefix: number
  gateway: string
  dns1: string
  dns2: string
}
type Config = { ethernet_support: boolean; selected: Link; wifi: IP; ethernet: IP }
type Net = {
  ssid: string
  network_id?: number
  current?: boolean
  signal?: number
  security?: 'open' | 'wpa2'
  compatible?: boolean
}
type Manager = {
  installed: boolean
  state_valid: boolean
  ready: boolean
  busy: boolean
  last_exit: number
  phase: string
  config: Config
  last_error: string
  diagnostic: string
  actual: Record<Link, { known: boolean; method: string; prefix: number; gateway: string; dns1: string; dns2: string }>
  pending: null | {
    phase: string
    seconds: number
    interface: Link
    ip: string
    token: string
    origin: string
    previous_origin: string
  }
  networks: null | { uptime_ms: number; saved: Net[]; visible: Net[] }
}
type Interface = {
  present: boolean
  enabled: boolean
  carrier: boolean | null
  address: string | null
  link_mbps: number | null
  driver: string
  rx_bytes: number | null
  tx_bytes: number | null
}
type Snapshot = {
  wifi: Interface
  ethernet: Interface
  wireless: {
    ssid: string
    security: string
    signal_dbm: number | null
    frequency_mhz: number | null
    control_available: boolean
  }
  route: { selected: string; source: string; internet: string }
  services: unknown
  usb?: { qualified_ue300: boolean; count: number }
}
type Envelope = { available: boolean; fresh: boolean; age_ms: number | null; snapshot: Snapshot | null }
const state = store({ data: null as Envelope | null, manager: null as Manager | null, ok: false, received: 0 })
let pending: Promise<void> | null = null
const refresh = () => {
  if (pending) return pending
  pending = (async () => {
    try {
      const [data, manager] = await Promise.all([
        request('/api/network') as Promise<Envelope>,
        request('/api/network/manager') as Promise<Manager>,
      ])
      state.set({ data, manager, ok: true, received: performance.now() })
    } catch {
      state.set({ ok: false })
    } finally {
      pending = null
    }
  })()
  return pending
}
const name = (link: Link) => t(link === 'wifi' ? 'network.wifi' : 'network.ethernet')
const Value = ({ label, value }: { label: Key; value: string | number | null | undefined }) => (
  <div class="min-w-0 rounded-md border border-edge/60 bg-field/50 px-3 py-2.5">
    <dt class="text-xs text-muted">{t(label)}</dt>
    <dd class="mt-1 font-semibold [overflow-wrap:anywhere]">{value ?? '—'}</dd>
  </div>
)
const blank: IP = { enabled: true, method: 'dhcp', ip: '', prefix: 24, gateway: '', dns1: '', dns2: '' }
export const Network = () => {
  usePoll(refresh, 2000)
  const { data, manager: m, ok, received } = state.use()
  const fresh = Boolean(ok && data?.fresh && (data.age_ms ?? 8000) + performance.now() - received < 8000)
  const d = fresh && data?.available ? data.snapshot : null
  const [working, setWorking] = useState(false),
    [error, setError] = useState('')
  const [ipLink, setIPLink] = useState<Link | null>(null),
    [ip, setIP] = useState<IP>(blank)
  const [wifiDialog, setWifiDialog] = useState(false),
    [ssid, setSSID] = useState(''),
    [security, setSecurity] = useState<'wpa2' | 'open'>('wpa2'),
    [password, setPassword] = useState('')
  const busy = working || Boolean(m?.busy)
  const locked = busy || !fresh || !m?.ready || !m.state_valid
  const run = async (body: Record<string, unknown>, confirmation = true) => {
    if (confirmation && !(await ask(t('network.confirm_change')))) return false
    setWorking(true)
    setError('')
    try {
      await request('/api/network/action', { method: 'POST', body: JSON.stringify(body) })
      await refresh()
      return true
    } catch (e) {
      setError(errText(e))
      return false
    } finally {
      setWorking(false)
    }
  }
  const scan = async () => {
    setError('')
    setWorking(true)
    try {
      await request('/api/network/scan', { method: 'POST', body: 'scan' })
      await refresh()
    } catch (e) {
      setError(errText(e))
    } finally {
      setWorking(false)
    }
  }
  const editIP = (link: Link) => {
    const c = m?.config[link],
      actual = m?.actual[link]
    setIP({
      ...blank,
      ...c,
      ip: c?.ip || d?.[link].address || '',
      prefix: c?.prefix || actual?.prefix || 24,
      gateway: c?.gateway || actual?.gateway || '',
      dns1: c?.dns1 || actual?.dns1 || '',
      dns2: c?.dns2 || actual?.dns2 || '',
    })
    setIPLink(link)
  }
  const p = m?.pending
  const here = Boolean(p?.ip && location.hostname === p.ip)
  const support = Boolean(m?.config.ethernet_support)
  return (
    <div class="grid gap-3.5">
      <Card>
        <CardHead
          icon="ethernet"
          title="network.support"
          end={
            <Switch
              on={support}
              label={t('network.support')}
              onClick={() => {
                if (!locked) void run({ op: 'support', enabled: !support })
              }}
            />
          }
        />
        <p class="text-xs text-muted">{t('network.support_description')}</p>
        {!m?.installed && (
          <p role="status" class="mt-2 text-amber">
            {t('network.manager_missing')}
          </p>
        )}
        {locked && !busy && <p class="mt-2 text-xs text-muted">{t('network.change_requirements')}</p>}
      </Card>
      {(error || m?.last_error || (m && m.last_exit > 0)) && (
        <p role="alert" class="rounded-md border border-red/50 bg-red/10 p-3 text-red [overflow-wrap:anywhere]">
          {error || t('network.action_failed')} {m?.last_error && <span class="text-xs">({m.last_error})</span>}
        </p>
      )}
      {p && (
        <Card>
          <CardHead icon="link" title="network.confirm_connection" />
          <p role="status">{t(p.phase === 'pending' ? 'network.confirm_description' : 'network.action_pending')}</p>
          <div class="mt-2 flex flex-wrap items-center gap-2">
            {p.origin && !here && (
              <a class="rounded-md border border-cyan px-3 py-2 text-cyan [overflow-wrap:anywhere]" href={p.origin}>
                {t('network.open_connection')}: {p.ip}
              </a>
            )}
            {here && p.phase === 'pending' && (
              <Button
                variant="primary"
                disabled={working}
                onClick={() => run({ op: 'confirm', token: p.token }, false)}
              >
                {t('network.keep_connection')}
              </Button>
            )}
            <Button
              disabled={working || p.phase === 'restoring'}
              onClick={() => run({ op: 'cancel', token: p.token }, false)}
            >
              {t('network.restore_previous')}
            </Button>
            {p.phase === 'pending' && (
              <span class="text-xs text-muted">
                {t('network.confirm_time')}: {p.seconds} s
              </span>
            )}
          </div>
        </Card>
      )}
      <div class={`grid gap-3.5 ${support ? 'cc2-md:grid-cols-2' : ''}`}>
        {(['wifi', ...(support ? (['ethernet'] as const) : [])] as Link[]).map(link => {
          const v = d?.[link],
            cfg = m?.config[link],
            selected = d?.route.selected === link
          return (
            <Card key={link} class="flex flex-col">
              <CardHead
                icon={link === 'wifi' ? 'wifi' : 'ethernet'}
                title={link === 'wifi' ? 'network.wifi' : 'network.ethernet'}
                end={
                  <Tag tone={selected ? 'ok' : 'default'}>
                    <Pip hollow={!selected} />
                    {t(selected ? 'network.selected' : v?.carrier ? 'network.connected' : 'network.inactive')}
                  </Tag>
                }
              />
              <div class="flex min-h-9 items-center justify-between gap-2">
                <h3 class="text-base font-semibold [overflow-wrap:anywhere]">
                  {link === 'wifi' ? d?.wireless.ssid || name(link) : name(link)}
                </h3>
                <Button
                  disabled={locked || !cfg}
                  onClick={() => run({ op: 'interface', interface: link, enabled: !cfg?.enabled })}
                >
                  {t(cfg?.enabled ? 'network.disable_interface' : 'network.enable_interface')}
                </Button>
              </div>
              <p class="mt-1 min-h-5 text-xs text-muted">
                {link === 'wifi'
                  ? d?.wireless.security || '—'
                  : t(v?.present ? 'network.interface_present' : 'network.missing_interface')}
              </p>
              <dl class="my-3 grid grid-cols-2 gap-2">
                <Value label="network.ip" value={v?.address} />
                <Value
                  label="network.assignment"
                  value={t(
                    m?.actual[link].known
                      ? m.actual[link].method === 'static'
                        ? 'network.static_ip'
                        : 'network.dhcp'
                      : 'network.unknown'
                  )}
                />
                {link === 'wifi' ? (
                  <>
                    <Value
                      label="network.signal"
                      value={d?.wireless.signal_dbm != null ? `${d.wireless.signal_dbm} dBm` : null}
                    />
                    <Value
                      label="network.frequency"
                      value={d?.wireless.frequency_mhz ? `${d.wireless.frequency_mhz} MHz` : null}
                    />
                  </>
                ) : (
                  <>
                    <Value label="network.speed" value={v?.link_mbps ? `${v.link_mbps} Mbit/s` : null} />
                    <Value label="network.gateway" value={m?.actual[link].gateway} />
                  </>
                )}
              </dl>
              <div class="mt-auto flex flex-wrap items-center gap-2">
                {link === 'wifi' && (
                  <Button
                    disabled={locked}
                    onClick={() => {
                      setWifiDialog(true)
                      void scan()
                    }}
                  >
                    {t('network.change_network')}
                  </Button>
                )}
                <Button disabled={locked} onClick={() => editIP(link)}>
                  {t('network.configure_ip')}
                </Button>
                <Button
                  variant={selected ? 'active' : 'primary'}
                  disabled={locked || selected}
                  onClick={() => run({ op: 'select', interface: link })}
                >
                  {t('network.switch')}
                </Button>
              </div>
            </Card>
          )
        })}
      </div>
      <Card>
        <CardHead
          icon="info"
          title="network.diagnostics"
          end={
            <Button disabled={working} onClick={refresh}>
              {t('network.refresh')}
            </Button>
          }
        />
        <p class="text-xs text-muted">{t('network.manual_selection')}</p>
        <details class="mt-2">
          <summary class="cursor-pointer text-cyan">{t('network.show_diagnostics')}</summary>
          <textarea
            readOnly
            rows={12}
            class="mt-2 max-h-72 w-full overflow-auto rounded-md bg-field p-3 text-xs"
            aria-label={t('network.diagnostics')}
            value={JSON.stringify(
              {
                fresh,
                phase: m?.phase,
                config: m?.config,
                snapshot: data?.snapshot,
                diagnostic: m?.diagnostic,
                last_error: m?.last_error,
              },
              null,
              2
            )}
          />
        </details>
      </Card>
      {ipLink && (
        <Dialog label={t('network.configure_ip')} locked={working} onClose={() => setIPLink(null)}>
          <DialogTitle class="mb-4">
            {t('network.configure_ip')} · {name(ipLink)}
          </DialogTitle>
          <form
            class="grid gap-3"
            onSubmit={async e => {
              e.preventDefault()
              const body =
                ip.method === 'dhcp'
                  ? { op: 'ip', interface: ipLink, method: 'dhcp' }
                  : {
                      op: 'ip',
                      interface: ipLink,
                      method: 'static',
                      ip: ip.ip.trim(),
                      prefix: Number(ip.prefix),
                      gateway: ip.gateway.trim(),
                      dns1: ip.dns1.trim(),
                      dns2: ip.dns2.trim(),
                    }
              if (await run(body)) setIPLink(null)
            }}
          >
            <label class="grid gap-1.5">
              {t('network.assignment')}
              <Select
                aria-label={t('network.assignment')}
                value={ip.method}
                onChange={e => setIP({ ...ip, method: e.currentTarget.value as 'dhcp' | 'static' })}
              >
                <option value="dhcp">{t('network.dhcp')}</option>
                <option value="static">{t('network.static_ip')}</option>
              </Select>
            </label>
            {ip.method === 'static' && (
              <div class="grid grid-cols-1 gap-3 cc2-sm:grid-cols-2">
                {(['ip', 'prefix', 'gateway', 'dns1', 'dns2'] as const).map(key => (
                  <label class="grid gap-1.5" key={key}>
                    {t(`network.${key}` as Key)}
                    <Input
                      value={ip[key]}
                      required={key === 'ip' || key === 'prefix'}
                      type={key === 'prefix' ? 'number' : 'text'}
                      inputMode={key === 'prefix' ? 'numeric' : 'decimal'}
                      min={key === 'prefix' ? 1 : undefined}
                      max={key === 'prefix' ? 30 : undefined}
                      onInput={e =>
                        setIP({
                          ...ip,
                          [key]: key === 'prefix' ? Number(e.currentTarget.value) : e.currentTarget.value,
                        })
                      }
                    />
                  </label>
                ))}
              </div>
            )}
            <div class="flex flex-wrap justify-end gap-2">
              <Button disabled={working} onClick={() => setIPLink(null)}>
                {t('common.cancel')}
              </Button>
              <Button type="submit" variant="primary" disabled={locked}>
                {t('network.apply')}
              </Button>
            </div>
          </form>
        </Dialog>
      )}
      {wifiDialog && (
        <Dialog
          label={t('network.change_network')}
          locked={working}
          onClose={() => {
            setWifiDialog(false)
            setPassword('')
          }}
        >
          <DialogTitle class="mb-4">{t('network.change_network')}</DialogTitle>
          <div class="mb-3 flex items-center justify-between gap-2">
            <h3 class="text-base font-semibold">{t('network.saved_networks')}</h3>
            <Button disabled={working} onClick={scan}>
              {t('network.scan')}
            </Button>
          </div>
          <div class="mb-3 grid max-h-40 gap-2 overflow-auto">
            {m?.networks?.saved.map(n => (
              <Button
                disabled={locked}
                class="justify-between"
                key={n.network_id}
                onClick={async () => {
                  if (await run({ op: 'wifi-saved', network_id: n.network_id })) {
                    setPassword('')
                    setWifiDialog(false)
                  }
                }}
              >
                <span class="min-w-0 truncate">{n.ssid}</span>
                {n.current && <span class="text-xs text-cyan">{t('network.selected')}</span>}
              </Button>
            ))}
          </div>
          <label class="grid gap-1.5">
            {t('network.visible_networks')}
            <Select
              aria-label={t('network.visible_networks')}
              value=""
              disabled={working}
              onChange={e => {
                const n = m?.networks?.visible[Number(e.currentTarget.value)]
                if (n) {
                  setSSID(n.ssid)
                  setSecurity(n.security || 'wpa2')
                  setPassword('')
                }
              }}
            >
              <option value="">{t('network.choose_network')}</option>
              {m?.networks?.visible.map((n, i) => (
                <option value={i} key={`${n.ssid}-${i}`} disabled={!n.compatible}>
                  {n.ssid} · {n.signal} dBm
                </option>
              ))}
            </Select>
          </label>
          <form
            class="mt-3 grid gap-3"
            onSubmit={async e => {
              e.preventDefault()
              const secret = password
              setPassword('')
              if (await run({ op: 'wifi-connect', ssid, security, password: security === 'open' ? '' : secret }))
                setWifiDialog(false)
            }}
          >
            <label class="grid gap-1.5">
              {t('network.ssid')}
              <Input required value={ssid} maxLength={32} onInput={e => setSSID(e.currentTarget.value)} />
            </label>
            <label class="grid gap-1.5">
              {t('network.security')}
              <Select
                aria-label={t('network.security')}
                value={security}
                onChange={e => setSecurity(e.currentTarget.value as 'open' | 'wpa2')}
              >
                <option value="wpa2">WPA / WPA2-PSK</option>
                <option value="open">{t('network.open_network')}</option>
              </Select>
            </label>
            {security === 'wpa2' && (
              <label class="grid gap-1.5" for="network-wifi-password">
                {t('network.password')}
                <PasswordInput
                  id="network-wifi-password"
                  value={password}
                  required
                  minLength={8}
                  maxLength={64}
                  autoComplete="new-password"
                  onInput={e => setPassword(e.currentTarget.value)}
                />
              </label>
            )}
            <div class="flex flex-wrap justify-end gap-2">
              <Button
                disabled={working}
                onClick={() => {
                  setWifiDialog(false)
                  setPassword('')
                }}
              >
                {t('common.cancel')}
              </Button>
              <Button type="submit" disabled={locked} variant="primary">
                {t('network.connect')}
              </Button>
            </div>
          </form>
        </Dialog>
      )}
    </div>
  )
}
