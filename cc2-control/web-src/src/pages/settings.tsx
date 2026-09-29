import { useState } from 'preact/hooks'
import { ask } from '@/lib/confirm'
import { cn } from '@/lib/utils'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Tag } from '@/components/ui/badge'
import { Input, Select } from '@/components/ui/field'
import { Activity, Info, Link, Palette, Plug, RefreshCw, Undo2 } from 'lucide-preact'
import { errText, notify, post, request } from '@/lib/api'
import { i18n, LANGUAGE_NAMES, setLanguage, setTheme, t, theme, tpl } from '@/lib/i18n'
import { ls } from '@/lib/store'
import { health } from '@/lib/state'

const TABS = [[Link, 'Connection'], [Plug, 'Integrations'], [Palette, 'Appearance'], [Info, 'About']] as const
const G = { size: 16, strokeWidth: 1 }
const Dot = () => <i class="inline-block size-2 rounded-full bg-current align-middle" />
const Field = ({ label, help, htmlFor, children }: { label: string; help?: string; htmlFor?: string; children: any }) => (
  <div class="my-2.5 grid items-center gap-x-3.5 gap-y-1 md:grid-cols-[170px_minmax(220px,480px)]">{htmlFor ? <label htmlFor={htmlFor} class="font-semibold">{t(label)}</label> : <span class="font-semibold">{t(label)}</span>}{children}{help && <span class="text-[10px] text-muted md:col-start-2">{t(help)}</span>}</div>
)
const Kv = ({ k, v }: { k: string; v: any }) => <><span>{t(k)}</span><b>{v}</b></>

const Connection = () => {
  const { data: h, setup } = health.use()
  const [code, setCode] = useState(''), [busy, setBusy] = useState(false)
  const mqtt = Boolean(h?.mqtt_connected && h?.mqtt_registered), ready = Boolean(setup?.configured && mqtt && setup?.snapshot_received)
  const verify = async () => {
    const v = code.trim()
    if (!v) return notify(t('Enter the new LAN access code.'), 'error')
    try {
      const s = await request('/api/setup'), re = Boolean(s.configured)
      if (re && !(await ask(t('Replace the saved LAN code? Only CC2 Control will restart.')))) return
      await post(re ? '/api/setup/revalidate' : '/api/setup', v)
      setCode(''); setBusy(true); notify(t('LAN access code saved. Restarting and synchronizing.'))
      setTimeout(() => location.reload(), 30000)
    } catch (e) { setBusy(false); notify(tpl('Verification failed: {error}', { error: errText(e) }), 'error') }
  }
  const test = async () => {
    try { await Promise.all([request('/api/health'), request('/api/printer'), request('/api/setup')]); notify(t('CC2 and MQTT connection are responding.')) } catch (e) { notify(tpl('Connection test failed: {error}', { error: errText(e) }), 'error') }
  }
  return (
    <Card><CardHead icon="link" title="Connection" sub="Network and device access settings" />
      <Field label="Printer IP" help="Current IP address (read-only)." htmlFor="printer-ip"><Input id="printer-ip" value={location.hostname} readOnly /></Field>
      <Field label="LAN Access Code" help="First launch requires the printer LAN access code." htmlFor="lan-code">
        <div class="flex"><Input id="lan-code" type="password" class="rounded-r-none" value={code} onInput={e => setCode(e.currentTarget.value)} /><Button class="rounded-l-none" disabled={busy} onClick={verify}>{t(setup?.configured ? 'Change / Revalidate' : 'Verify')}</Button></div>
        <span class={cn('text-xs md:col-start-2', ready ? 'text-green' : 'text-amber')}><Dot />  {busy ? t('Restarting…') : t(!setup ? 'Checking configuration…' : ready ? 'Configured' : setup.configured ? 'Revalidation required' : 'Configuration required')}</span>
      </Field>
      <Field label="MQTT Connection"><div class="flex flex-wrap items-center gap-3"><Tag tone={mqtt ? 'ok' : 'warning'}><Dot /> {t(mqtt ? 'Connected' : 'Waiting')}</Tag><Button onClick={test}><RefreshCw {...G} />{t('Reconnect / Test')}</Button></div></Field>
    </Card>
  )
}

const Integrations = () => {
  const ping = async (url: string, ok: string, fail: string, parse?: (r: any) => string) => {
    try {
      const r = await fetch(url)
      if (!r.ok) throw Error(String(r.status))
      const m = parse ? tpl(ok, { text: parse(await r.json()) }) : t(ok)
      notify(m)
    } catch (e) { notify(tpl(fail, { error: errText(e) })) }
  }
  const row = 'my-2 grid items-center gap-2.5 md:grid-cols-[230px_1fr_auto]'
  return (
    <Card><CardHead icon="plug" title="Integrations" sub="Service configuration for local integrations" />
      <div class={row}><b>{t('Panda Breath Compatibility')}</b><span class="text-muted">{t('Read-only compatibility endpoint')}</span><Button onClick={() => ping(`http://${location.hostname}:7125/server/info`, 'Panda Breath compatibility endpoint is available.', 'Panda endpoint unavailable: {error}')}><Activity {...G} />{t('Test Endpoint')}</Button></div>
      <div class={row}><b>{t('OrcaSlicer Compatibility')}</b><span class="text-muted">{t('Octo/Klipper test endpoint')}</span><Button onClick={() => ping('/api/version', 'Orca compatibility ready: {text}', 'Orca compatibility unavailable: {error}', v => v.text)}><Activity {...G} />{t('Test Orca')}</Button></div>
    </Card>
  )
}

const Appearance = () => {
  const lang = i18n.get().lang, mode = theme.use().mode
  const restore = async () => {
    if (!(await ask(t('Restore interface preferences? Printer configuration and LAN code will not be changed.'), true))) return
    ;['cc2-language', 'cc2-theme'].forEach(ls.del)
    setTheme('dark'); setLanguage('en', true); notify(t('Interface preferences restored.'))
  }
  const lab = 'text-[11px] font-semibold'
  return (
    <>
      <Card><CardHead icon="palette" title="Appearance" sub="Interface and display preferences" />
        <div class="grid gap-3 sm:grid-cols-2">
          <label class={lab}>{t('Language')}<Select class="mt-1.5" value={lang} onChange={e => setLanguage(e.currentTarget.value, true)}>{Object.entries(LANGUAGE_NAMES).map(([c, n]) => <option key={c} value={c}>{n}</option>)}</Select></label>
          <label class={lab}>{t('Theme')}<Select class="mt-1.5" value={mode} onChange={e => setTheme(e.currentTarget.value, true)}><option value="dark">{t('Dark')}</option><option value="light">{t('Light')}</option></Select></label>
        </div></Card>
      <div class="mt-3 flex flex-wrap items-center gap-3 border-t border-edge pt-3">
        <div class="mr-auto"><b>{t('Configuration Actions')}</b><br /><small class="text-muted">{t('Interface preferences are stored on the printer and shared by every browser.')}</small></div>
        <Button variant="danger" onClick={restore}><Undo2 {...G} />{t('Restore Defaults')}</Button>
      </div>
    </>
  )
}

export const Settings = () => {
  const [tab, setTab] = useState(0)
  const h = health.use().data
  return (
    <Card class="mx-auto max-w-[1180px]">
      <Page title="Settings" sub="Configure your CC2 printer and application preferences">
        <div class="mx-auto mb-3 grid max-w-[860px] grid-cols-2 overflow-hidden rounded-lg border border-edge sm:grid-cols-4">
          {TABS.map(([Glyph, label], i) => <button type="button" key={label} onClick={() => setTab(i)} class={cn('flex items-center justify-center gap-2 border-edge px-3 py-2 text-[13px] sm:border-r sm:last:border-0', tab === i ? 'bg-field text-cyan' : 'hover:bg-field/50')}><Glyph {...G} />{t(label)}</button>)}
        </div>
        <div class="mx-auto max-w-[860px]">
          {tab === 0 && <Connection />}
          {tab === 1 && <Integrations />}
          {tab === 2 && <Appearance />}
          {tab === 3 && <Card><CardHead icon="info" title="About" /><div class="mx-auto grid max-w-[650px] grid-cols-[190px_1fr] gap-2 text-xs"><Kv k="CC2 Control Version" v={h?.version || '—'} /><Kv k="Operating Mode" v={h?.mode || '—'} /><Kv k="Service Type" v={t('Local service')} /><Kv k="Project" v="CC2 Control Community" /></div></Card>}
        </div>
      </Page>
    </Card>
  )
}
