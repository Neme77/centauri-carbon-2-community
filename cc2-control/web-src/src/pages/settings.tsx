import { useEffect, useRef, useState } from 'preact/hooks'
import { ask } from '@/lib/confirm'
import { cn } from '@/lib/utils'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Tag } from '@/components/ui/badge'
import { Input, PasswordInput, Select, Switch } from '@/components/ui/field'
import { Tabs } from '@/components/ui/tabs'
import { GithubIcon } from '@/components/github-icon'
import { Activity, Info, Link, Palette, Plug, RefreshCw, Undo2, Archive, FileDown } from 'lucide-preact'
import { errText, notify, post, request } from '@/lib/api'
import { collectDiagnostics } from '@/lib/diagnostics'
import {
  cameraPreferences,
  setPersistentCamera,
  detectLanguage,
  i18n,
  type Key,
  LANGUAGE_OPTIONS,
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
import {
  pandabreath,
  pandabreathDefaultHost,
  pandabreathHost,
  pandabreathName,
  setPandaBreathEnabled,
  setPandaBreathFirmware,
  setPandaBreathHost,
} from '@/lib/pandabreath'
import { health, nav, openSettings, type SettingsTab, refreshHealth, refreshSetup } from '@/lib/state'
import { Updates } from './Updates'
import { Backup } from './Backup'

const TABS = [
  [Link, 'settings.connection'],
  [Plug, 'settings.integrations'],
  [Palette, 'settings.appearance'],
  [Info, 'settings.about'],
  [RefreshCw, 'updates.title'],
  [Archive, 'backup.title'],
] as const
const TAB_ROUTES: SettingsTab[] = ['connection', 'integrations', 'appearance', 'about', 'updates', 'backup']
const G = { size: 16, strokeWidth: 1 }
const Dot = () => <i class="inline-block size-2 rounded-full bg-current align-middle" />
const Field = ({ label, help, htmlFor, children }: { label: Key; help?: Key; htmlFor?: string; children: any }) => (
  <div class="my-2.5 grid items-center gap-x-3.5 gap-y-1 cc2-md:grid-cols-[170px_minmax(220px,480px)]">
    {htmlFor ? (
      <label htmlFor={htmlFor} class="font-semibold">
        {t(label)}
      </label>
    ) : (
      <span class="font-semibold">{t(label)}</span>
    )}
    {children}
    {help && <span class="text-xs text-muted cc2-md:col-start-2">{t(help)}</span>}
  </div>
)
const Kv = ({ k, text, v }: { k?: Key; text?: string; v: any }) => (
  <>
    <span>{text ?? (k && t(k))}</span>
    <b>{v}</b>
  </>
)

const Connection = () => {
  const restartTimer = useRef(0)
  useEffect(() => () => clearTimeout(restartTimer.current), [])
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
      restartTimer.current = window.setTimeout(() => location.reload(), 30000)
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
        <div class="cc2-lan-code-row flex min-w-0 items-center gap-2">
          <PasswordInput
            id="lan-code"
            class="min-w-0 flex-1"
            value={code}
            onInput={e => setCode(e.currentTarget.value)}
          />
          <Button class="shrink-0 whitespace-nowrap px-3" disabled={busy} onClick={verify}>
            {t(setup?.configured ? 'settings.change_revalidate' : 'settings.verify')}
          </Button>
        </div>
        <span class={cn('text-xs cc2-md:col-start-2', ready ? 'text-green' : 'text-amber')}>
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
  const checking = useRef(false)
  const [orcaBusy, setOrcaBusy] = useState(false)
  // Test the actual Moonraker and Canvas sync endpoints, not only the app version.
  const testOrca = async () => {
    if (checking.current) return
    checking.current = true
    setOrcaBusy(true)
    try {
      const info = await request('/server/info')
      if (info?.result?.klippy_connected !== true) {
        notify(t('settings.orca_sync_disconnected'), 'error')
        return
      }
      const lanes = await request('/server/database/item?namespace=lane_data')
      const value = lanes?.result?.value
      if (lanes?.result?.namespace !== 'lane_data' || !value || typeof value !== 'object' || Array.isArray(value))
        throw Error(t('settings.orca_sync_pending'))
      const count = Object.entries(value).filter(([key, row]) => {
        if (!/^lane[1-4]$/.test(key) || !row || typeof row !== 'object' || Array.isArray(row)) return false
        const entry = row as Record<string, unknown>
        return (
          entry.lane === String(Number(key.slice(4)) - 1) &&
          typeof entry.material === 'string' &&
          entry.material.trim().length > 0 &&
          typeof entry.color === 'string' &&
          /^(?:#|0x)?[0-9a-f]{6}(?:[0-9a-f]{2})?$/i.test(entry.color)
        )
      }).length
      if (count) notify(tpl('settings.orca_sync_ready', { count }))
      else notify(t('settings.orca_sync_pending'))
    } catch (error) {
      notify(tpl('settings.orca_compatibility_unavailable', { error: errText(error) }), 'error')
    } finally {
      checking.current = false
      setOrcaBusy(false)
    }
  }
  const ping = async (url: string, ok: Key, fail: Key, parse?: (r: any) => string) => {
    try {
      const r = await fetch(url)
      if (!r.ok) throw Error(String(r.status))
      const m = parse ? tpl(ok, { text: parse(await r.json()) }) : t(ok)
      notify(m)
    } catch (e) {
      notify(tpl(fail, { error: errText(e) }), 'error')
    }
  }
  const row = 'my-2 grid items-center gap-2.5 cc2-md:grid-cols-[230px_1fr_auto]'
  const db = pandabreath.use()
  const dbName = { name: t(pandabreathName(db)) }
  const dbDefault = pandabreathDefaultHost(db.dragonbreath)
  const savedDbHost = pandabreathHost(db)
  const [dbHost, setDbHost] = useState(savedDbHost)
  useEffect(() => setDbHost(savedDbHost), [savedDbHost]) // the firmware switch changes the default address
  const saveDbHost = () => {
    if (setPandaBreathHost(dbHost)) notify(tpl('settings.pandabreath_address_saved', dbName))
    else notify(tpl('settings.pandabreath_address_invalid', { host: dbDefault }), 'error')
  }
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
        <Button disabled={orcaBusy} onClick={testOrca}>
          <Activity {...G} />
          {t('settings.test_orca')}
        </Button>
      </div>
      <div class={row}>
        <b>{tpl('settings.pandabreath_page', dbName)}</b>
        <span class="text-muted">{tpl('settings.pandabreath_page_description', dbName)}</span>
        <Switch
          on={db.enabled}
          label={tpl('settings.pandabreath_page', dbName)}
          onClick={() => setPandaBreathEnabled(!db.enabled)}
        />
      </div>
      {db.enabled && (
        <div class={row}>
          <b>{t('settings.pandabreath_dragonbreath')}</b>
          <span class="text-muted">{t('settings.pandabreath_dragonbreath_description')}</span>
          <Switch
            on={db.dragonbreath}
            label={t('settings.pandabreath_dragonbreath')}
            onClick={() => setPandaBreathFirmware(!db.dragonbreath)}
          />
        </div>
      )}
      {db.enabled && (
        <div class={row}>
          <label for="pandabreath-host">
            <b>{tpl('settings.pandabreath_address', dbName)}</b>
          </label>
          <Input
            id="pandabreath-host"
            value={dbHost}
            placeholder={dbDefault}
            spellcheck={false}
            autocapitalize="off"
            onInput={e => setDbHost(e.currentTarget.value)}
            onKeyDown={e => e.key === 'Enter' && saveDbHost()}
          />
          <Button onClick={saveDbHost}>{t('settings.save_address')}</Button>
        </div>
      )}
    </Card>
  )
}

const Appearance = () => {
  const savedPersistent = cameraPreferences.use().persistent
  const [persistent, setPersistent] = useState(savedPersistent)
  useEffect(() => setPersistent(savedPersistent), [savedPersistent])
  const [savingCamera, setSavingCamera] = useState(false)
  const changeCamera = async (enabled: boolean) => {
    setPersistent(enabled)
    setSavingCamera(true)
    try {
      await setPersistentCamera(enabled)
    } catch (e) {
      setPersistent(savedPersistent)
      notify(errText(e), 'error')
    } finally {
      setSavingCamera(false)
    }
  }
  const lang = i18n.get().lang,
    mode = theme.use().mode
  const restore = async () => {
    if (!(await ask(t('settings.restore_interface_preferences'), true))) return
    try {
      await saveQuickActions(QUICK_DEFAULTS)
    } catch (e) {
      notify(errText(e), 'error')
      return
    }
    await changeCamera(false)
    ;['cc2-language', 'cc2-theme'].forEach(ls.del)
    setTheme('dark', true)
    await setLanguage(detectLanguage(), true)
    notify(t('settings.interface_preferences_restored'))
  }
  const lab = 'text-xs font-semibold'
  return (
    <>
      <Card>
        <CardHead icon="palette" title="settings.appearance" sub="settings.interface_and_display_preferences" />
        <div class="grid gap-3 cc2-sm:grid-cols-2">
          <label class={lab}>
            {t('settings.language')}
            <Select class="mt-1.5" value={lang} onChange={e => setLanguage(e.currentTarget.value, true)}>
              {LANGUAGE_OPTIONS.map(([c, n]) => (
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
        <label class="mt-4 flex items-start gap-3 border-t border-edge pt-3">
          <input
            type="checkbox"
            class="mt-1 accent-cyan"
            checked={persistent}
            disabled={savingCamera}
            onChange={e => void changeCamera(e.currentTarget.checked)}
          />
          <span>
            <b>{t('settings.camera_persistent')}</b>
            <small class="mt-1 block text-muted">{t('settings.camera_persistent_help')}</small>
          </span>
        </label>
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

const DiagnosticsExport = () => {
  const [busy, setBusy] = useState(false)
  const download = async () => {
    if (busy) return
    setBusy(true)
    try {
      const report = await collectDiagnostics(request)
      const json = `${JSON.stringify(report, null, 2)}\n`
      if (json.length > 16384) throw Error(t('state.unknown'))
      const blob = new Blob([json], { type: 'application/json;charset=utf-8' })
      const url = URL.createObjectURL(blob)
      const link = document.createElement('a')
      link.href = url
      link.download = `CC2-Control-diagnostics-${new Date().toLocaleDateString('sv-SE')}.json`
      document.body.appendChild(link)
      link.click()
      link.remove()
      window.setTimeout(() => URL.revokeObjectURL(url), 1000)
      notify(t('settings.diagnostics_downloaded'))
    } catch (error) {
      notify(tpl('settings.diagnostics_failed_error', { error: errText(error) }), 'error')
    } finally {
      setBusy(false)
    }
  }
  return (
    <div class="mt-4 border-t border-edge pt-3">
      <p class="mb-3 max-w-2xl text-xs text-muted">{t('settings.diagnostics_help')}</p>
      <Button disabled={busy} onClick={() => void download()}>
        <FileDown {...G} />
        {t('settings.diagnostics_export')}
      </Button>
    </div>
  )
}

export const Settings = () => {
  usePoll(refreshHealth, 5000)
  usePoll(refreshSetup, 5000)
  const tab = TAB_ROUTES.indexOf(nav.use().settings)
  const h = health.use().data
  return (
    <div class="cc2-settings mx-auto max-w-4xl">
      <Page title="common.settings" sub="settings.configure_your_cc2_printer_and">
        <Tabs
          wrap
          items={TABS.map(([Glyph, label]) => ({ id: label, label: t(label), icon: <Glyph {...G} /> }))}
          value={TABS[tab][1]}
          onChange={id => openSettings(TAB_ROUTES[TABS.findIndex(([, label]) => label === id)])}
        />
        <div class="max-w-4xl">
          {tab === 0 && <Connection />}
          {tab === 1 && <Integrations />}
          {tab === 2 && <Appearance />}
          {tab === 4 && <Updates />}
          {tab === 5 && <Backup />}
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
              <DiagnosticsExport />
            </Card>
          )}
        </div>
      </Page>
    </div>
  )
}
