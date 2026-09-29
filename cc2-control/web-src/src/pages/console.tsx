import { useEffect, useRef, useState } from 'preact/hooks'
import { ask } from '@/lib/confirm'
import { Activity, Check as CheckIcon, Copy, Send, Trash2, TriangleAlert } from 'lucide-preact'
import { store } from '@/lib/store'
import { cn } from '@/lib/utils'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Pip } from '@/components/ui/badge'
import { Input, Switch } from '@/components/ui/field'
import { Icon } from '@/components/icons'
import { errText, notify, post } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { consoleLog, printer, refreshConsole, view } from '@/lib/state'

const I = { size: 16, strokeWidth: 1 }
const expert = store({ on: false })
const history: string[] = [] // commands sent in this session, for the arrow-key recall

export async function sendConsole(command: string) {
  command = command.trim()
  if (!command) return
  try {
    await post('/api/console/command', command)
    await refreshConsole()
  } catch (e) {
    const stamp = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' })
    consoleLog.set(s => ({ text: `${s.text}\n[${stamp}]  CC2 Control: command rejected — ${errText(e)}`.trim() }))
    notify(tpl('Command rejected: {error}', { error: errText(e) }), 'error')
  }
}

const Check = ({ children }: { children: string }) => (
  <div class="flex gap-2.5">
    <CheckIcon {...I} class="shrink-0 text-cyan" />
    {t(children)}
  </div>
)

export const Console = () => {
  const { text } = consoleLog.use()
  const on = expert.use().on
  const d = printer.use().data,
    v = view(d)
  const [filter, setFilter] = useState(''),
    [auto, setAuto] = useState(true),
    [cmd, setCmd] = useState(''),
    recall = useRef(-1)
  const screen = useRef<HTMLPreElement>(null)
  useEffect(() => {
    refreshConsole()
  }, [])
  // The two placeholder lines come from this UI, everything else is printer output and stays as received.
  const lines = (/^Protected console ready\.( Waiting for live printer output\.)?$/.test(text) ? t(text) : text).split(
      /\r?\n/
    ),
    shown = (filter ? lines.filter(l => l.toLowerCase().includes(filter.toLowerCase())) : lines).join('\n')
  useEffect(() => {
    if (auto && screen.current) screen.current.scrollTop = screen.current.scrollHeight
  }, [shown, auto])

  const toggleExpert = async () => {
    if (on) return expert.set({ on: false })
    if (
      await ask(
        t(
          'WARNING: Manual G-code can move axes, heat components and alter calibration. The console is blocked during printing; movement requires homing and direct G0/G1 moves are range checked. Continue?'
        ),
        true
      )
    )
      expert.set({ on: true })
  }
  const send = () => {
    const c = cmd.trim()
    if (!c) return
    if (history[history.length - 1] !== c) history.push(c)
    recall.current = -1
    setCmd('')
    sendConsole(c)
  }
  const browse = (e: KeyboardEvent) => {
    if (e.key === 'Enter') return send()
    if ((e.key !== 'ArrowUp' && e.key !== 'ArrowDown') || !history.length) return
    e.preventDefault()
    recall.current = e.key === 'ArrowUp' ? Math.min(recall.current + 1, history.length - 1) : recall.current - 1
    setCmd(recall.current < 0 ? '' : history[history.length - 1 - recall.current])
  }
  const clear = async () => {
    try {
      await post('/api/console/clear')
      consoleLog.set({ text: '' })
      await refreshConsole()
    } catch (e) {
      notify(errText(e), 'error')
    }
  }
  return (
    <>
      <div class="grid gap-3.5 xl:grid-cols-[minmax(0,2.3fr)_minmax(330px,1fr)]">
        <Card>
          <div class="mb-4">
            <h2 class="text-2xl font-semibold">{t('Console')}</h2>
            <p class="text-muted">{t('Send G-code commands and view printer output (advanced)')}</p>
          </div>
          <div class="mb-3 flex flex-wrap items-center gap-2.5">
            <Input
              class="min-w-48 flex-1"
              placeholder={t('Filter output (e.g. error, echo, ok)')}
              value={filter}
              onInput={e => setFilter(e.currentTarget.value)}
            />
            <label class="flex items-center gap-2">
              {t('Auto-scroll')} <Switch on={auto} label={t('Auto-scroll')} onClick={() => setAuto(!auto)} />
            </label>
            <Button onClick={clear}>
              <Trash2 {...I} />
              {t('Clear Output')}
            </Button>
            <Button
              onClick={() =>
                navigator.clipboard
                  .writeText(screen.current?.textContent || '')
                  .then(() => notify(t('Console copied.')))
              }
            >
              <Copy {...I} />
              {t('Copy Log')}
            </Button>
          </div>
          <pre
            ref={screen}
            class="m-0 h-[26rem] overflow-auto whitespace-pre-wrap rounded-md border border-edge bg-[#031521] p-3.5 font-mono text-[#edf6fc] text-[13px] leading-7"
          >
            {shown}
          </pre>
          <label htmlFor="console-command" class="mb-2 mt-4 block font-semibold">
            {t('Send G-code command')}
          </label>
          <div class="flex gap-2.5">
            <Input
              id="console-command"
              class="flex-1"
              disabled={!on}
              placeholder={t(on ? 'Enter G-code command' : 'Unlock console to send G-code…')}
              value={cmd}
              onInput={e => setCmd(e.currentTarget.value)}
              onKeyDown={browse}
              autoComplete="off"
              spellcheck={false}
            />
            <Button disabled={!on} onClick={send}>
              <Send {...I} />
              {t('Send')}
            </Button>
          </div>
        </Card>
        <div class="grid content-start gap-3">
          <Card>
            <CardHead icon="lock" title="Protected Terminal" />
            <div
              class={cn(
                'flex gap-3 rounded-md border p-3',
                on ? 'border-green bg-green/10 text-green' : 'border-red bg-red/10 text-red'
              )}
            >
              <Icon n={on ? 'unlock' : 'lock'} class="size-7" />
              <div>
                <strong class="block text-[15px]">{t(on ? 'Unlocked' : 'Locked')}</strong>
                <small>
                  {t(
                    on
                      ? 'Console enabled for this dashboard session. Backend safety guards remain active.'
                      : 'Confirm the warning to enable manual G-code.'
                  )}
                </small>
              </div>
            </div>
            <div class="mt-2 flex gap-3 rounded-md border border-amber bg-amber/10 p-3 text-amber">
              <TriangleAlert size={30} strokeWidth={1} class="shrink-0" />
              <div>
                <strong class="block text-[15px]">
                  {t('Arbitrary G-code can move axes, heat components, or damage the printer.')}
                </strong>
                <small>{t('Only send commands you understand.')}</small>
              </div>
            </div>
            <div class="mt-3 border-t border-edge pt-3">
              <h3 class="mb-2 font-semibold">{t('Unlock Expert Mode')}</h3>
              <Button wide variant="primary" onClick={toggleExpert}>
                <Icon n={on ? 'lock' : 'unlock'} />
                <span>{t(on ? 'Lock console' : 'Unlock console')}</span>
              </Button>
              <small class="mt-2 block text-muted">{t('Available only while the printer is idle.')}</small>
            </div>
          </Card>
          <Card>
            <CardHead icon="settings" title="Safety Rules" />
            <div class="grid gap-3.5 text-[13px]">
              {[
                'Motion commands require homing',
                'Terminal locks when a print starts',
                'Simple local confirmation; no password or token',
                'Kinematic-bypass commands remain blocked',
              ].map(s => (
                <Check key={s}>{s}</Check>
              ))}
            </div>
          </Card>
          <Card>
            <CardHead icon="file" title="Safe Commands" end={t('Available when idle')} />
            <div class="grid grid-cols-2 gap-2">
              {[
                ['M105', 'Temperatures'],
                ['M114', 'Position'],
                ['STATUS', 'Printer status'],
                ['HELP', 'Show help'],
              ].map(([c, l]) => (
                <Button key={c} class="min-h-14 flex-col text-xs" onClick={() => sendConsole(c)}>
                  <b>{c}</b>
                  <small>{t(l)}</small>
                </Button>
              ))}
            </div>
          </Card>
        </div>
      </div>
      <div class="mt-3.5 flex justify-between border-t border-edge px-1 pt-3 text-xs text-muted">
        <span class="flex items-center gap-2">
          <Activity {...I} /> CC2 {t('Connected')} <Pip hollow /> {t(v.state)}
        </span>
        <span>
          {t('Messages received')}: {d?.messages || 0}
        </span>
      </div>
    </>
  )
}
