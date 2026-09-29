import { render } from 'preact'
import './index.css'
import { App } from './App'
import { setTheme, theme, loadUiPreferences } from '@/lib/i18n'
import { poll } from '@/lib/poll'
import { loadPresets, refreshHealth, refreshPrinter } from '@/lib/state'
import { checkOrcaPendingPrint } from '@/pages/print-dialog'

setTheme(theme.get().mode)
void loadUiPreferences()
void loadPresets()
// Sources that always run (the header needs them). Pages poll faster only while they are open, see usePoll.
poll(refreshPrinter, 3000)
poll(refreshHealth, 30000)
// An OrcaSlicer "Upload and Print" must be noticed even when its embedded WebView reports the page as hidden,
// and its timers can be throttled, so this one keeps running when hidden and also checks on interaction.
poll(checkOrcaPendingPrint, 1500, { hidden: true })
for (const ev of ['visibilitychange', 'focus', 'pageshow']) addEventListener(ev, () => void checkOrcaPendingPrint())
for (const ev of ['pointerenter', 'pointerdown'])
  addEventListener(ev, () => void checkOrcaPendingPrint(), { passive: true })

const root = document.getElementById('app')
if (root) render(<App />, root)
