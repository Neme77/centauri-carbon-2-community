import { useState } from 'preact/hooks'
import { ask } from '@/lib/confirm'
import { cn } from '@/lib/utils'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Tag } from '@/components/ui/badge'
import { Input, Select } from '@/components/ui/field'
import { Tabs } from '@/components/ui/tabs'
import { GithubIcon } from '@/components/github-icon'
import { Activity, Info, Link, Palette, Plug, RefreshCw, Undo2 } from 'lucide-preact'
import { errText, notify, post, request } from '@/lib/api'
import {
  detectLanguage,
  i18n,
  type Key,
  LANGUAGE_NAMES,
  setLanguage,
  setTheme,
  t,
  THEMES,
  theme,
  tpl,
} from '@/lib/i18n'
import { QUICK_DEFAULTS, saveQuickActions } from '@/lib/quick'
import { ls } from '@/lib/store'
import { usePoll } from '@/lib/poll'
import { health, refreshHealth, refreshSetup } from '@/lib/state'

const TABS = [
  [Link, 'settings.connection'],
  [Plug, 'settings.integrations'],
  [Palette, 'settings.appearance'],
  [Info, 'settings.about'],
] as const
const G = { size: 16, strokeWidth: 1 }
const Dot = () => <i class="inline-block size-2 rounded-full bg-current align-middle" />
const Field = ({ label, help, htmlFor, children }: { label: Key; help?: Key; htmlFor?: string; children: any }) => (
  <div class="my-2.5 grid items-center gap-x-3.5 gap-y-1 @min-[768px]/page:grid-cols-[170px_minmax(220px,480px)]">
    {htmlFor ? (
      <label htmlFor={htmlFor} class="font-semibold">
        {t(label)}
      </label>
    ) : (
      <span class="font-semibold">{t(label)}</span>
    )}
    {children}
    {help && <span class="text-[10px] text-muted @min-[768px]/page:col-start-2">{t(help)}</span>}
  </div>
)
const Kv = ({ k, text, v }: { k?: Key; text?: string; v: any }) => (
  <>
    <span>{text ?? (k && t(k))}</span>
    <b>{v}</b>
  </>
)

const Connection = () => {
  const { data: h, setup } = health.use()
  const [code, setCode] = useState(''),
    [busy, setBusy] = useState(false)
  const mqtt = Boolean(h?.mqtt_connected && h?.mqtt_registered),
    ready = Boolean(setup?.configured && mqtt && setup?.snapshot_received)
  const verify = async () => {
    const v = code.trim()
    if (!v) return notify(t('settings.enter_the_new_lan_access_code'), 'error')
    try {
      const s = await request('/api/setup'),
        re = Boolean(s.configured)
      if (re && !(await ask(t('settings.replace_the_saved_lan_code_only')))) return
      await post(re ? '/api/setup/revalidate' : '/api/setup', v)
      setCode('')
      setBusy(true)
      notify(t('settings.lan_access_code_saved_restarting'))
      setTimeout(() => location.reload(), 30000)
    } catch (e) {
      setBusy(false)
      notify(tpl('settings.verification_failed_error', { error: errText(e) }), 'error')
    }
  }
  const test = async () => {
    try {
      await Promise.all([request('/api/health'), request('/api/printer'), request('/api/setup')])
      notify(t('settings.cc2_and_mqtt_connection_are'))
    } catch (e) {
      notify(tpl('settings.connection_test_failed_error', { error: errText(e) }), 'error')
    }
  }
  return (
    <Card>
      <CardHead icon="link" title="settings.connection" sub="settings.network_and_device_access_settings" />
      <Field label="settings.printer_ip" help="settings.current_ip_address_read_only" htmlFor="printer-ip">
        <Input id="printer-ip" value={location.hostname} readOnly />
      </Field>
      <Field label="settings.lan_access_code" help="settings.first_launch_requires_the_printer" htmlFor="lan-code">
        <div class="flex">
          <Input
            id="lan-code"
            type="password"
            class="rounded-r-none"
            value={code}
            onInput={e => setCode(e.currentTarget.value)}
          />
          <Button class="rounded-l-none" disabled={busy} onClick={verify}>
            {t(setup?.configured ? 'settings.change_revalidate' : 'settings.verify')}
          </Button>
        </div>
        <span class={cn('text-xs @min-[768px]/page:col-start-2', ready ? 'text-green' : 'text-amber')}>
          <Dot />{' '}
          {busy
            ? t('settings.restarting')
            : t(
                !setup
                  ? 'common.checking_configuration'
                  : ready
                    ? 'settings.configured'
                    : setup.configured
                      ? 'settings.revalidation_required'
                      : 'settings.configuration_required'
              )}
        </span>
      </Field>
      <Field label="settings.mqtt_connection">
        <div class="flex flex-wrap items-center gap-3">
          <Tag tone={mqtt ? 'ok' : 'warning'}>
            <Dot /> {t(mqtt ? 'common.connected' : 'settings.waiting')}
          </Tag>
          <Button onClick={test}>
            <RefreshCw {...G} />
            {t('settings.reconnect_test')}
          </Button>
        </div>
      </Field>
    </Card>
  )
}

const Integrations = () => {
  const ping = async (url: string, ok: Key, fail: Key, parse?: (r: any) => string) => {
    try {
      const r = await fetch(url)
      if (!r.ok) throw Error(String(r.status))
      const m = parse ? tpl(ok, { text: parse(await r.json()) }) : t(ok)
      notify(m)
    } catch (e) {
      notify(tpl(fail, { error: errText(e) }))
    }
  }
  const row = 'my-2 grid items-center gap-2.5 @min-[768px]/page:grid-cols-[230px_1fr_auto]'
  return (
    <Card>
      <CardHead icon="plug" title="settings.integrations" sub="settings.service_configuration_for_local" />
      <div class={row}>
        <b>{t('settings.panda_breath_compatibility')}</b>
        <span class="text-muted">{t('settings.read_only_compatibility_endpoint')}</span>
        <Button
          onClick={() =>
            ping(
              `http://${location.hostname}:7125/server/info`,
              'settings.panda_endpoint_available',
              'settings.panda_endpoint_unavailable_error'
            )
          }
        >
          <Activity {...G} />
          {t('settings.test_endpoint')}
        </Button>
      </div>
      <div class={row}>
        <b>{t('settings.orcaslicer_compatibility')}</b>
        <span class="text-muted">{t('settings.octo_klipper_test_endpoint')}</span>
        <Button
          onClick={() =>
            ping(
              '/api/version',
              'settings.orca_compatibility_ready_text',
              'settings.orca_compatibility_unavailable',
              v => v.text
            )
          }
        >
          <Activity {...G} />
          {t('settings.test_orca')}
        </Button>
      </div>
    </Card>
  )
}

const Appearance = () => {
  const lang = i18n.get().lang,
    mode = theme.use().mode
  const restore = async () => {
    if (!(await ask(t('settings.restore_interface_preferences'), true))) return
    ;['cc2-language', 'cc2-theme'].forEach(ls.del)
    setTheme('dark', true)
    void saveQuickActions(QUICK_DEFAULTS)
    setLanguage(detectLanguage(), true)
    notify(t('settings.interface_preferences_restored'))
  }
  const lab = 'text-[11px] font-semibold'
  return (
    <>
      <Card>
        <CardHead icon="palette" title="settings.appearance" sub="settings.interface_and_display_preferences" />
        <div class="grid gap-3 @min-[640px]/page:grid-cols-2">
          <label class={lab}>
            {t('settings.language')}
            <Select class="mt-1.5" value={lang} onChange={e => setLanguage(e.currentTarget.value, true)}>
              {Object.entries(LANGUAGE_NAMES).map(([c, n]) => (
                <option key={c} value={c}>
                  {n}
                </option>
              ))}
            </Select>
          </label>
          <label class={lab}>
            {t('settings.theme')}
            <Select class="mt-1.5" value={mode} onChange={e => setTheme(e.currentTarget.value, true)}>
              {THEMES.map(x => (
                <option key={x.id} value={x.id}>
                  {x.key ? t(x.key) : x.label}
                </option>
              ))}
            </Select>
          </label>
        </div>
      </Card>
      <div class="mt-3 flex flex-wrap items-center gap-3 border-t border-edge pt-3">
        <div class="mr-auto">
          <b>{t('settings.configuration_actions')}</b>
          <br />
          <small class="text-muted">{t('settings.interface_preferences_are_stored')}</small>
        </div>
        <Button variant="danger" onClick={restore}>
          <Undo2 {...G} />
          {t('settings.restore_defaults')}
        </Button>
      </div>
    </>
  )
}

export const Settings = () => {
  usePoll(refreshHealth, 5000)
  usePoll(refreshSetup, 5000)
  const [tab, setTab] = useState(0)
  const h = health.use().data
  return (
    <Page title="common.settings" sub="settings.configure_your_cc2_printer_and">
      <Tabs
        items={TABS.map(([Glyph, label]) => ({ id: label, label: t(label), icon: <Glyph {...G} /> }))}
        value={TABS[tab][1]}
        onChange={id => setTab(TABS.findIndex(([, label]) => label === id))}
      />
      <div class="max-w-4xl">
        {tab === 0 && <Connection />}
        {tab === 1 && <Integrations />}
        {tab === 2 && <Appearance />}
        {tab === 3 && (
          <Card>
            <CardHead icon="info" title="settings.about" />
            <p class="mb-4 max-w-2xl text-[13px] leading-relaxed">{t('settings.cc2_control_is_the_local_control')}</p>
            <div class="grid max-w-2xl grid-cols-[190px_1fr] gap-2 text-xs">
              <Kv k="settings.version" v={h?.version || '—'} />
              <Kv
                text="GitHub"
                v={
                  <a
                    href="https://github.com/Neme77/centauri-carbon-2-community"
                    target="_blank"
                    rel="noopener noreferrer"
                    class="inline-flex items-center gap-2 font-normal text-cyan underline-offset-2 hover:underline"
                  >
                    <GithubIcon class="size-4" />
                    Neme77/centauri-carbon-2-community
                  </a>
                }
              />
              <Kv
                k="settings.libraries"
                v={<span class="font-normal">{t('settings.preact_tailwind_css_lucide_icons')}</span>}
              />
            </div>
          </Card>
        )}
      </div>
    </Page>
  )
}
