import { useEffect, useRef, useState } from 'preact/hooks'
import { cn } from '@/lib/utils'
import { CanvasIcon } from '@/components/canvas-icon'
import { HeaderTitle } from '@/components/header-title'
import { PrinterIcon } from '@/components/printer-icon'
import {
  Menu,
  RotateCw,
  Files as FilesIcon,
  Gauge,
  Grid3x3,
  History,
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
import { control, errText, notify, post, toast } from '@/lib/api'
import { ask } from '@/lib/confirm'
import { printerError, printerRequest } from '@/lib/machine'
import { refreshPrinter, health, menu, mobileMenu, nav, printer, toggleMenu, view, type Page } from '@/lib/state'

// Same query as the compact layout in index.css.
const COMPACT = '(max-width: 900px) and (orientation: portrait), (max-height: 500px) and (orientation: landscape)'
type NavIcon = typeof Gauge | typeof CanvasIcon
const items: [Page, NavIcon, Key][] = [
  ['dashboard', Gauge, 'app.dashboard'],
  ['control', SlidersHorizontal, 'common.control'],
  ['job', ListChecks, 'common.job'],
  ['files', FilesIcon, 'common.files'],
  ['history', History, 'common.history'],
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
    if (e.type === 'keydown' && (e as KeyboardEvent).repeat) return
    cancel()
    fired.current = false
    setHolding(true)
    timer.current = window.setTimeout(async () => {
      timer.current = 0
      fired.current = true
      setHolding(false)
      if (await control('system:emergency_stop')) await refreshPrinter()
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
      onBlur={cancel}
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

const RestartPrinter = () => {
  const { data, ok } = printer.use()
  const [busy, setBusy] = useState(false)
  const available = ok && data?.recovery?.available && !data?.recovery?.reboot_pending && !busy
  const restart = async () => {
    if (!available || !(await ask(t('recovery.confirm'), true))) return
    setBusy(true)
    try {
      await post('/api/recovery/reboot', 'REBOOT_AFTER_EMERGENCY')
      notify(t('recovery.restarting'))
      await refreshPrinter()
    } catch (e) {
      notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
    } finally {
      setBusy(false)
    }
  }
  return (
    <button
      type="button"
      disabled={!available}
      onClick={restart}
      title={t(available ? 'recovery.restart' : 'recovery.requires_emergency')}
      aria-label={t('recovery.restart')}
      class="flex h-11 shrink-0 items-center justify-center gap-2 rounded-md border border-edge px-3 disabled:opacity-40 md:px-4"
    >
      <RotateCw size={20} strokeWidth={1} />
      <span class="hidden md:inline">{t('recovery.restart')}</span>
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
    const media = matchMedia(COMPACT)
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
        <nav
          class="mt-2 grid min-h-0 flex-1 content-start gap-0.5 overflow-y-auto"
          aria-label={t('app.main_navigation')}
        >
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
        <div class="mt-auto hidden shrink-0 border-t border-edge md:block">
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
          <div>
            {link[0] === 'green' ? (
              <>
                {tState(v.state)}
                {v.detail && <span class="hidden md:inline"> · {v.detail}</span>}
                {v.active ? ` · ${Math.round(v.progress)}%` : ''}
              </>
            ) : (
              '—'
            )}
          </div>
        </div>
        <div class="hidden h-11 items-center gap-2 rounded-md border border-edge px-3 sm:flex">
          <Dot c={link[0]} />
          {t(link[1])}
        </div>
        <EStop />
        <RestartPrinter />
      </div>
      {v.active && (
        <div class="absolute inset-x-0 bottom-0 h-1 bg-edge" role="progressbar" aria-valuenow={Math.round(v.progress)}>
          <i class="block h-full bg-cyan transition-[width]" style={{ width: `${v.progress}%` }} />
        </div>
      )}
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

// The printer answers CC2 Control's MQTT requests after the HTTP request has returned; each new refusal
// is shown once. An old refusal seen when the page opens is not news.
export const RefusalWatcher = () => {
  const refusal = printer.use().data?.printer_error
  const seen = useRef<number | null>(null)
  useEffect(() => {
    if (!refusal || refusal.sequence === seen.current) return
    const first = seen.current === null
    seen.current = refusal.sequence
    if (first && Number(refusal.age) > 15) return
    notify(
      tpl('printer.refused', {
        request: printerRequest(Number(refusal.method)),
        error: printerError(Number(refusal.code)),
        code: Number(refusal.code),
      }),
      'error'
    )
  }, [refusal?.sequence])
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

// No extra poll: retain the latest native event in the existing printer response.
export const PrinterReport = () => {
  const { data, ok } = printer.use()
  const report = data?.printer_report
  const [dismissed, setDismissed] = useState<number | null>(null)
  if (!report || dismissed === report.sequence) return null
  const title =
    report.level === 3
      ? 'printer.report_resume'
      : report.level === 2
        ? 'printer.report_critical'
        : 'printer.report_warning'
  return (
    <section role="alert" data-testid="printer-report" class="mb-4 rounded-lg border border-warning bg-panel p-4">
      <div class="flex items-start justify-between gap-3">
        <strong>
          {t(title)} · {tpl('printer.report_code', { code: report.code })}
        </strong>
        <button type="button" aria-label={t('printer.report_dismiss')} onClick={() => setDismissed(report.sequence)}>
          <X size={18} />
        </button>
      </div>
      {report.message && <p class="mt-2 whitespace-pre-wrap break-words">{report.message}</p>}
      <p class="mt-2 text-xs text-muted">
        {tpl('printer.report_last', { seconds: report.age })} {t('printer.report_history')}
      </p>
      {(!ok || !data.connected) && <p class="text-xs text-muted">{t('printer.report_disconnected')}</p>}
    </section>
  )
}
