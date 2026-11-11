import { render } from 'preact'
import './index.css'
import { App } from './App'
import { setTheme, theme, loadUiPreferences } from '@/lib/i18n'
import { checkUpdateNotice, UPDATE_RETRY_INTERVAL } from '@/lib/updates'
import { poll } from '@/lib/poll'
import { loadPresets, nav, refreshHealth, refreshPrinter } from '@/lib/state'
import { checkOrcaPendingPrint } from '@/pages/print-dialog'

const root = document.getElementById('app')
setTheme(theme.get().mode)
if (root)
  render(
    <div
      class="flex min-h-screen items-center justify-center gap-3 bg-bg text-fg"
      role="status"
      aria-label="CC2 Control"
    >
      <i class="size-5 animate-spin rounded-full border-2 border-current border-t-transparent" />
      <span>CC2 Control</span>
    </div>,
    root
  )

// Both requests have bounded waits. Never render known-wrong English labels first;
// failed requests still reach the usable application instead of an offline shell.
async function start() {
  setTheme(theme.get().mode)
  await loadUiPreferences()
  // Sources that always run (the header needs them). Pages poll faster only while they are open, see usePoll.
  poll(refreshPrinter, 3000)
  // The separate camera window has no header, material presets or print dialog: it only needs the printer state.
  if (!nav.get().camera) {
    void loadPresets()
    poll(refreshHealth, 30000)
    poll(checkUpdateNotice, UPDATE_RETRY_INTERVAL)
    // An OrcaSlicer "Upload and Print" must be noticed even when its embedded WebView reports the page as hidden,
    // and its timers can be throttled, so this one keeps running when hidden and also checks on interaction.
    poll(checkOrcaPendingPrint, 1500, { hidden: true })
    for (const ev of ['visibilitychange', 'focus', 'pageshow']) addEventListener(ev, () => void checkOrcaPendingPrint())
    for (const ev of ['pointerenter', 'pointerdown'])
      addEventListener(ev, () => void checkOrcaPendingPrint(), { passive: true })
  }

  if (root) render(<App />, root)
}
void start()
