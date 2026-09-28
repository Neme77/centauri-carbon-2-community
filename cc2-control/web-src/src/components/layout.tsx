import { useEffect, useRef, useState } from 'preact/hooks'
import { cn } from '@/lib/utils'
import { Icon } from '@/components/icons'
import { Boxes, Files as FilesIcon, Gauge, Grid3x3, ListChecks, Settings, SlidersHorizontal, PanelLeftClose, PanelLeftOpen, SquareTerminal, TriangleAlert } from 'lucide-preact'
import { Dot } from '@/components/ui/badge'
import { t } from '@/lib/i18n'
import { control, notify, toast } from '@/lib/api'
import { health, menu, nav, printer, toggleMenu, view, type Page } from '@/lib/state'

const items: [Page, typeof Gauge, string][] = [
  ['dashboard', Gauge, 'Dashboard'], ['control', SlidersHorizontal, 'Control'], ['job', ListChecks, 'Job'], ['files', FilesIcon, 'Files'],
  ['bed', Grid3x3, 'Bed Levelling'], ['canvas', Boxes, 'Canvas'], ['console', SquareTerminal, 'Console'], ['settings', Settings, 'Settings'],
]

// Hold for one second to trigger the emergency stop; a plain click only explains how.
const EStop = () => {
  const [holding, setHolding] = useState(false)
  const timer = useRef(0), fired = useRef(false)
  const cancel = () => { clearTimeout(timer.current); timer.current = 0; setHolding(false) }
  const start = (e: Event) => {
    if (e.type === 'keydown' && !['Enter', ' '].includes((e as KeyboardEvent).key)) return
    e.preventDefault(); cancel(); fired.current = false; setHolding(true)
    timer.current = window.setTimeout(async () => { timer.current = 0; fired.current = true; setHolding(false); await control('system:emergency_stop') }, 1000)
  }
  return (
    <button type="button" aria-label={t('Hold for emergency stop')} onPointerDown={start} onPointerUp={cancel} onPointerCancel={cancel} onPointerLeave={cancel} onKeyDown={start} onKeyUp={cancel}
      onClick={e => { e.preventDefault(); if (!fired.current) notify(t('Hold Emergency Stop for one second.')) }}
      class="relative ml-2 flex min-h-11 items-center justify-center overflow-hidden rounded-lg border border-red bg-red/10 px-3 font-bold text-red md:px-4">
      <i class={cn('absolute inset-y-0 left-0 bg-red/30', holding ? 'w-full transition-[width] duration-1000 ease-linear' : 'w-0')} />
      <span class="relative flex items-center gap-2"><TriangleAlert size={20} strokeWidth={1} /><span class="hidden md:inline">{t('EMERGENCY STOP')}</span></span>
    </button>
  )
}

export const Sidebar = () => {
  const { page } = nav.use()
  const h = health.use().data
  const { collapsed } = menu.use()
  return (
    <aside class={cn('fixed bottom-0 left-0 top-17 z-20 flex w-18.5 flex-col border-r border-edge bg-panel transition-[width]', !collapsed && 'md:w-52')}>
      <nav class="mt-2 grid gap-0.5" aria-label="Main navigation">
        {items.map(([p, Glyph, label]) => (
          <a key={p} href={`#${p}`} aria-current={page === p ? 'page' : undefined} title={t(label)}
            class={cn('flex h-13 items-center justify-center gap-3.5 border-l-4 border-transparent hover:bg-field', !collapsed && 'md:justify-start md:px-4', page === p && 'border-cyan bg-field text-cyan')}>
            <Glyph size={24} strokeWidth={1} class="shrink-0" /><span class={cn('hidden', !collapsed && 'md:inline')}>{t(label)}</span>
          </a>
        ))}
      </nav>
      <div class="mt-auto hidden border-t border-edge md:block">
        <button type="button" onClick={toggleMenu} aria-expanded={!collapsed} title={t(collapsed ? 'Expand menu' : 'Collapse menu')}
          class={cn('flex h-11 w-full items-center justify-center gap-3.5 text-muted hover:bg-field hover:text-fg', !collapsed && 'md:justify-start md:px-5')}>
          {collapsed ? <PanelLeftOpen size={22} strokeWidth={1} /> : <PanelLeftClose size={22} strokeWidth={1} />}{!collapsed && <span>{t('Collapse menu')}</span>}
        </button>
        {!collapsed && <div class="px-5 pb-3 text-center text-xs text-muted">{t('Version:')} <b class="font-medium tabular-nums text-fg">{h ? `v${h.version}` : '—'}</b></div>}
      </div>
    </aside>
  )
}

export const Topbar = () => {
  const { data: d, ok } = printer.use()
  const v = view(d)
  // One link indicator: CC2 Control reachable → MQTT session up → printer messages recent (3 missed 10 s heartbeats = stale).
  const link: ['green' | 'amber' | 'red', string] = !ok ? ['red', 'Printer unreachable'] : !d ? ['amber', 'Connecting to the printer…'] : !d.connected ? ['red', 'Reconnecting to the printer…']
    : d.last_message_age < 0 || d.last_message_age > 30 ? ['amber', 'Waiting for the printer'] : ['green', 'Printer connected']
  return (
    <header class="fixed inset-x-0 top-0 z-30 flex h-17 items-center justify-between gap-4 border-b border-edge bg-panel px-4 md:px-5">
      <h1 class="text-xl font-bold leading-tight text-cyan md:text-2xl">Centauri Carbon 2 - Control Center</h1>
      <div class="flex items-center gap-2 text-xs">
        <div class="flex items-center gap-2 rounded-md border border-edge px-3 py-1.5"><Icon n="monitor" class="hidden sm:block" /><div><strong class="block text-sm">CC2</strong><small class="text-muted">{t(v.state === 'Unknown' && !d ? 'Idle' : v.state)}</small></div></div>
        <div class="hidden items-center gap-2 rounded-md border border-edge px-3 py-1.5 sm:flex"><Dot c={link[0]} />{t(link[1])}</div>
        <EStop />
      </div>
    </header>
  )
}

export const Toast = () => {
  const { text, n, tone } = toast.use()
  const [show, setShow] = useState(false)
  useEffect(() => {
    if (!n) return
    setShow(true)
    const id = setTimeout(() => setShow(false), tone === 'error' ? 7000 : 3600)
    return () => clearTimeout(id)
  }, [n])
  return show ? <div role={tone === 'error' ? 'alert' : 'status'} onClick={() => setShow(false)} title={t('Dismiss')} class={cn('fixed bottom-6 left-1/2 z-[70] max-w-[90%] -translate-x-1/2 cursor-pointer rounded-lg border bg-panel px-5 py-3 shadow-2xl', tone === 'error' ? 'border-red text-red' : 'border-cyan')}>{text}</div> : null
}
