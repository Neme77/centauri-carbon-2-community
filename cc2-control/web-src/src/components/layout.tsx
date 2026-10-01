import { useEffect, useRef, useState } from 'preact/hooks'
import { cn } from '@/lib/utils'
import { CanvasIcon } from '@/components/canvas-icon'
import { HeaderTitle } from '@/components/header-title'
import { PrinterIcon } from '@/components/printer-icon'
import {
  Menu,
  Files as FilesIcon,
  Gauge,
  Grid3x3,
  ListChecks,
  Settings,
  SlidersHorizontal,
  PanelLeftClose,
  PanelLeftOpen,
  SquareTerminal,
  TriangleAlert,
  X,
} from 'lucide-preact'
import { Dot } from '@/components/ui/badge'
import { type Key, t, tState, tpl } from '@/lib/i18n'
import { control, notify, toast } from '@/lib/api'
import { health, menu, mobileMenu, nav, printer, toggleMenu, view, type Page } from '@/lib/state'

type NavIcon = typeof Gauge | typeof CanvasIcon
const items: [Page, NavIcon, Key][] = [
  ['dashboard', Gauge, 'app.dashboard'],
  ['control', SlidersHorizontal, 'common.control'],
  ['job', ListChecks, 'common.job'],
  ['files', FilesIcon, 'common.files'],
  ['bed', Grid3x3, 'common.bed_levelling'],
  ['canvas', CanvasIcon, 'common.canvas'],
  ['console', SquareTerminal, 'common.console'],
  ['settings', Settings, 'common.settings'],
]

// Hold for one second to trigger the emergency stop; a plain click only explains how.
const EStop = () => {
  const [holding, setHolding] = useState(false)
  const timer = useRef(0),
    fired = useRef(false)
  const cancel = () => {
    clearTimeout(timer.current)
    timer.current = 0
    setHolding(false)
  }
  const start = (e: Event) => {
    if (e.type === 'keydown' && !['Enter', ' '].includes((e as KeyboardEvent).key)) return
    e.preventDefault()
    cancel()
    fired.current = false
    setHolding(true)
    timer.current = window.setTimeout(async () => {
      timer.current = 0
      fired.current = true
      setHolding(false)
      await control('system:emergency_stop')
    }, 1000)
  }
  return (
    <button
      type="button"
      aria-label={t('app.hold_for_emergency_stop')}
      onPointerDown={start}
      onPointerUp={cancel}
      onPointerCancel={cancel}
      onPointerLeave={cancel}
      onKeyDown={start}
      onKeyUp={cancel}
      onContextMenu={e => e.preventDefault()}
      onClick={e => {
        e.preventDefault()
        if (!fired.current) notify(t('app.hold_emergency_stop_for_one_second'))
      }}
      class="relative flex h-11 touch-none select-none items-center justify-center overflow-hidden rounded-md border border-red bg-red/10 px-3 font-bold text-red md:px-4"
    >
      <i
        class={cn(
          'absolute inset-y-0 left-0 bg-red/30',
          holding ? 'w-full transition-[width] duration-1000 ease-linear' : 'w-0'
        )}
      />
      <span class="relative flex items-center gap-2">
        <TriangleAlert size={20} strokeWidth={1} />
        <span class="hidden md:inline">{t('app.emergency_stop')}</span>
      </span>
    </button>
  )
}

export const Sidebar = () => {
  const { page } = nav.use()
  const h = health.use().data
  const { collapsed } = menu.use()
  const { open } = mobileMenu.use()
  const aside = useRef<HTMLElement>(null)
  useEffect(() => {
    if (!open) {
      if (aside.current?.contains(document.activeElement)) document.getElementById('cc2-menu-toggle')?.focus()
      return
    }
    const media = matchMedia('(max-width: 767px) and (orientation: portrait)')
    const close = () => mobileMenu.set({ open: false })
    const closeOnEscape = (e: KeyboardEvent) => {
      if (e.key === 'Escape') close()
      if (e.key === 'Tab') {
        const toggle = document.getElementById('cc2-menu-toggle')
        const links = Array.from(aside.current?.querySelectorAll<HTMLAnchorElement>('a') || [])
        const targets = [toggle, ...links].filter((el): el is HTMLElement => Boolean(el?.getClientRects().length))
        const current = targets.indexOf(document.activeElement as HTMLElement)
        if (targets.length && (current < 0 || (e.shiftKey ? current === 0 : current === targets.length - 1))) {
          e.preventDefault()
          targets[e.shiftKey ? targets.length - 1 : 0].focus()
        }
      }
    }
    const changed = () => {
      if (!media.matches) close()
    }
    const oldOverflow = document.body.style.overflow
    document.body.style.overflow = 'hidden'
    aside.current?.querySelector<HTMLAnchorElement>('a')?.focus()
    document.addEventListener('keydown', closeOnEscape)
    media.addEventListener('change', changed)
    return () => {
      document.body.style.overflow = oldOverflow
      document.removeEventListener('keydown', closeOnEscape)
      media.removeEventListener('change', changed)
    }
  }, [open])
  return (
    <>
      {open && (
        <button
          type="button"
          class="cc2-menu-backdrop"
          aria-label={t('app.collapse_menu')}
          onClick={() => mobileMenu.set({ open: false })}
        />
      )}

      <aside
        id="cc2-navigation"
        ref={aside}
        class={cn(
          'cc2-sidebar',
          open && 'cc2-sidebar-open',
          'fixed bottom-0 left-0 top-17 z-20 flex w-18.5 flex-col border-r border-edge bg-panel transition-[width]',
          !collapsed && 'md:w-52'
        )}
      >
        <nav class="mt-2 grid gap-0.5" aria-label={t('app.main_navigation')}>
          {items.map(([p, Glyph, label]) => (
            <a
              key={p}
              href={`#${p}`}
              onClick={() => mobileMenu.set({ open: false })}
              aria-current={page === p ? 'page' : undefined}
              title={t(label)}
              class={cn(
                'flex h-13 items-center justify-center gap-3.5 border-l-4 border-transparent hover:bg-field',
                !collapsed && 'md:justify-start md:px-4',
                page === p && 'border-cyan bg-field text-cyan'
              )}
            >
              <Glyph size={24} strokeWidth={1} class="shrink-0" />
              <span class={cn('hidden', !collapsed && 'md:inline')}>{t(label)}</span>
            </a>
          ))}
        </nav>
        <div class="mt-auto hidden border-t border-edge md:block">
          <button
            type="button"
            onClick={toggleMenu}
            aria-expanded={!collapsed}
            title={t(collapsed ? 'app.expand_menu' : 'app.collapse_menu')}
            class={cn(
              'flex h-11 w-full items-center justify-center gap-3.5 text-muted hover:bg-field hover:text-fg',
              !collapsed && 'md:justify-start md:px-5'
            )}
          >
            {collapsed ? <PanelLeftOpen size={22} strokeWidth={1} /> : <PanelLeftClose size={22} strokeWidth={1} />}
            {!collapsed && <span>{t('app.collapse_menu')}</span>}
          </button>
          {!collapsed && (
            <div class="px-5 pb-3 text-center text-xs text-muted">
              {t('app.version')} <b class="font-medium tabular-nums text-fg">{h ? `${h.version}` : '—'}</b>
            </div>
          )}
        </div>
      </aside>
    </>
  )
}

export const Topbar = () => {
  const { open } = mobileMenu.use()
  const { data: d, ok } = printer.use()
  const v = view(d)
  // The state under "CC2" is only shown while the link is healthy; otherwise it could be stale or unknown.
  // One link indicator: CC2 Control reachable → MQTT session up → printer messages recent (3 missed 10 s heartbeats = stale).
  const link: ['green' | 'amber' | 'red', Key] = !ok
    ? ['red', 'app.printer_unreachable']
    : !d
      ? ['amber', 'app.connecting_to_the_printer']
      : !d.connected
        ? ['red', 'app.reconnecting_to_the_printer']
        : d.last_message_age < 0 || d.last_message_age > 30
          ? ['amber', 'app.waiting_for_the_printer']
          : ['green', 'app.printer_connected']
  return (
    <header class="cc2-topbar fixed inset-x-0 top-0 z-30 flex h-17 items-center justify-between gap-4 border-b border-edge bg-panel px-4 md:px-5 xl:pl-2">
      <button
        type="button"
        id="cc2-menu-toggle"
        class="cc2-mobile-menu hidden h-11 w-11 shrink-0 items-center justify-center rounded-md border border-edge"
        aria-label={t(open ? 'app.collapse_menu' : 'app.expand_menu')}
        aria-expanded={open}
        aria-controls="cc2-navigation"
        onClick={() => mobileMenu.set({ open: !open })}
      >
        <Menu size={22} strokeWidth={1} />
      </button>
      <HeaderTitle />
      <div class="flex items-center gap-2">
        <div class="flex h-11 items-center gap-2 rounded-md border border-edge px-3">
          <PrinterIcon class="hidden size-6 sm:block" />
          <div>{link[0] === 'green' ? tState(v.state) : '—'}</div>
        </div>
        <div class="hidden h-11 items-center gap-2 rounded-md border border-edge px-3 sm:flex">
          <Dot c={link[0]} />
          {t(link[1])}
        </div>
        <EStop />
      </div>
    </header>
  )
}

// Keeps the browser tab title informative: current page, and print progress while a job runs.
export const TitleSync = () => {
  const { page } = nav.use()
  const v = view(printer.use().data)
  const label = items.find(i => i[0] === page)?.[2] ?? 'app.dashboard'
  document.title = `${v.active ? `${Math.round(v.progress)}% · ` : ''}${t(label)} · Centauri Carbon 2`
  return null
}

// The printer reports no end-of-print event, so a print that was active and no longer is has ended (finished or cancelled).
export const PrintWatcher = () => {
  const d = printer.use().data
  const running = useRef<string | null>(null)
  useEffect(() => {
    if (!d) return
    const v = view(d)
    if (v.active) running.current = v.rawFilename
    else if (running.current !== null) {
      notify(tpl('app.print_ended', { name: running.current }))
      running.current = null
    }
  }, [d])
  return null
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
  return show ? (
    <div
      role={tone === 'error' ? 'alert' : 'status'}
      class={cn(
        'fixed bottom-6 left-1/2 z-[70] flex max-w-[90%] -translate-x-1/2 items-center gap-3 rounded-lg border bg-panel py-3 pl-5 pr-3 shadow-2xl',
        tone === 'error' ? 'border-red text-red' : 'border-cyan'
      )}
    >
      <span>{text}</span>
      <button
        type="button"
        onClick={() => setShow(false)}
        aria-label={t('app.dismiss')}
        title={t('app.dismiss')}
        class="rounded p-1 hover:bg-field"
      >
        <X size={16} strokeWidth={1} />
      </button>
    </div>
  ) : null
}
