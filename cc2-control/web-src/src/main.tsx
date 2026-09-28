import { render } from 'preact'
import './index.css'
import { App } from './App'
import { setTheme, theme, loadUiPreferences } from '@/lib/i18n'
import { loadPresets, refreshConsole, refreshHealth, refreshPrinter } from '@/lib/state'
import { checkOrcaPendingPrint } from '@/pages/print-dialog'

setTheme(theme.get().mode)
void loadUiPreferences()
void loadPresets()
for (const f of [refreshHealth, refreshPrinter, refreshConsole, checkOrcaPendingPrint]) void f()
setInterval(refreshHealth, 5000)
setInterval(refreshPrinter, 1500)
setInterval(refreshConsole, 2500)
// Timers can be throttled while OrcaSlicer hides its embedded WebView, so also check on interaction.
setInterval(checkOrcaPendingPrint, 1500)
for (const ev of ['visibilitychange', 'focus', 'pageshow']) addEventListener(ev, () => void checkOrcaPendingPrint())
addEventListener('pointerdown', () => void checkOrcaPendingPrint(), { passive: true })

render(<App />, document.getElementById('app')!)
