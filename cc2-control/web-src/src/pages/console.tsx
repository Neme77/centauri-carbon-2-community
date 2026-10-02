import { useEffect, useRef, useState } from 'preact/hooks'
import { ask } from '@/lib/confirm'
import { copyText } from '@/lib/clipboard'
import { Activity, Check as CheckIcon, Copy, Send, Trash2, TriangleAlert } from 'lucide-preact'
import { store } from '@/lib/store'
import { cn } from '@/lib/utils'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Pip } from '@/components/ui/badge'
import { Input, Switch } from '@/components/ui/field'
import { Icon } from '@/components/icons'
import { errText, notify, post } from '@/lib/api'
import { type Key, t, tState, tpl } from '@/lib/i18n'
import { usePoll } from '@/lib/poll'
import { consoleLog, printer, refreshConsole, view } from '@/lib/state'

const I = { size: 16, strokeWidth: 1 }
const expert = store({ on: false })
const history: string[] = [] // commands sent in this session, for the arrow-key recall

export async function sendConsole(command: string) {
  command = command.trim()
  if (!command) return false
  try {
    await post('/api/console/command', command)
    await refreshConsole()
    return true
  } catch (e) {
    const stamp = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' })
    consoleLog.set(s => ({ text: `${s.text ?? ''}\n[${stamp}]  CC2 Control: command rejected — ${errText(e)}`.trim() }))
    notify(tpl('console.command_rejected_error', { error: errText(e) }), 'error')
    return false
  }
}

const Check = ({ children }: { children: Key }) => (
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
  usePoll(refreshConsole, 2500)
  // The placeholder comes from this UI, everything else is printer output and stays as received.
  const lines = (text ?? t('common.protected_console_ready_waiting')).split(/\r?\n/),
    shown = (filter ? lines.filter(l => l.toLowerCase().includes(filter.toLowerCase())) : lines).join('\n')
  useEffect(() => {
    if (auto && screen.current) screen.current.scrollTop = screen.current.scrollHeight
  }, [shown, auto])

  const toggleExpert = async () => {
    if (on) return expert.set({ on: false })
    if (await ask(t('console.warning_manual_g_code_can_move'), true)) expert.set({ on: true })
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
    <Page title="common.console" sub="console.send_g_code_commands_and_view">
      <div class="grid gap-3.5 cc2-xl:grid-cols-[minmax(0,2.3fr)_minmax(330px,1fr)]">
        <Card>
          <div class="mb-3 flex flex-wrap items-center gap-2.5">
            <Input
              class="min-w-48 flex-1"
              placeholder={t('console.filter_output_e_g_error_echo')}
              value={filter}
              onInput={e => setFilter(e.currentTarget.value)}
            />
            <label class="flex items-center gap-2">
              {t('console.auto_scroll')}{' '}
              <Switch on={auto} label={t('console.auto_scroll')} onClick={() => setAuto(!auto)} />
            </label>
            <Button onClick={clear}>
              <Trash2 {...I} />
              {t('console.clear_output')}
            </Button>
            <Button
              onClick={async () => {
                try {
                  await copyText(screen.current?.textContent || '')
                  notify(t('console.console_copied'))
                } catch (e) {
                  notify(errText(e), 'error')
                }
              }}
            >
              <Copy {...I} />
              {t('console.copy_log')}
            </Button>
          </div>
          <pre
            ref={screen}
            class="m-0 h-[26rem] overflow-auto whitespace-pre-wrap rounded-md border border-edge bg-well p-3.5 font-mono text-well-fg text-[13px] leading-7"
          >
            {shown}
          </pre>
          <label htmlFor="console-command" class="mb-2 mt-4 block font-semibold">
            {t('console.send_g_code_command')}
          </label>
          <div class="flex gap-2.5">
            <Input
              id="console-command"
              class="flex-1"
              disabled={!on}
              placeholder={t(on ? 'console.enter_g_code_command' : 'console.unlock_console_to_send_g_code')}
              value={cmd}
              onInput={e => setCmd(e.currentTarget.value)}
              onKeyDown={browse}
              autoComplete="off"
              spellcheck={false}
            />
            <Button disabled={!on} onClick={send}>
              <Send {...I} />
              {t('console.send')}
            </Button>
          </div>
        </Card>
        <div class="grid content-start gap-3">
          <Card>
            <CardHead icon="lock" title="console.protected_terminal" />
            <div
              class={cn(
                'flex gap-3 rounded-md border p-3',
                on ? 'border-green bg-green/10 text-green' : 'border-red bg-red/10 text-red'
              )}
            >
              <Icon n={on ? 'unlock' : 'lock'} class="size-7" />
              <div>
                <strong class="block text-[15px]">{t(on ? 'console.unlocked' : 'console.locked')}</strong>
                <small>
                  {t(on ? 'console.console_enabled_for_this_dashboard' : 'console.confirm_the_warning_to_enable')}
                </small>
              </div>
            </div>
            <div class="mt-2 flex gap-3 rounded-md border border-amber bg-amber/10 p-3 text-amber">
              <TriangleAlert size={30} strokeWidth={1} class="shrink-0" />
              <div>
                <strong class="block text-[15px]">{t('console.arbitrary_g_code_can_move_axes')}</strong>
                <small>{t('console.only_send_commands_you_understand')}</small>
              </div>
            </div>
            <div class="mt-3 border-t border-edge pt-3">
              <h3 class="mb-2 font-semibold">{t('console.unlock_expert_mode')}</h3>
              <Button wide variant="primary" onClick={toggleExpert}>
                <Icon n={on ? 'lock' : 'unlock'} />
                <span>{t(on ? 'console.lock_console' : 'console.unlock_console')}</span>
              </Button>
              <small class="mt-2 block text-muted">{t('console.available_only_while_the_printer')}</small>
            </div>
          </Card>
          <Card>
            <CardHead icon="settings" title="console.safety_rules" />
            <div class="grid gap-3.5 text-[13px]">
              {(
                [
                  'console.motion_commands_require_homing',
                  'console.terminal_locks_when_a_print_starts',
                  'console.simple_local_confirmation_no',
                  'console.kinematic_bypass_commands_remain',
                ] as Key[]
              ).map(s => (
                <Check key={s}>{s}</Check>
              ))}
            </div>
          </Card>
          <Card>
            <CardHead icon="file" title="console.safe_commands" end={t('common.available_when_idle')} />
            <div class="grid grid-cols-2 gap-2">
              {(
                [
                  ['M105', 'common.temperatures'],
                  ['M114', 'common.position'],
                  ['STATUS', 'console.printer_status'],
                  ['HELP', 'console.show_help'],
                ] as [string, Key][]
              ).map(([c, l]) => (
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
          <Activity {...I} /> CC2 {t('common.connected')} <Pip hollow /> {tState(v.state)}
        </span>
        <span>
          {t('console.messages_received')}: {d?.messages || 0}
        </span>
      </div>
    </Page>
  )
}
