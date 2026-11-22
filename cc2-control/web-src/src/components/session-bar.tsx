import { useEffect, useState } from 'preact/hooks'
import { Button } from '@/components/ui/button'
import { ScrewCancelButton } from '@/components/calibration-bar'
import { TriangleAlert } from 'lucide-preact'
import { control, errText, notify } from '@/lib/api'
import { ask } from '@/lib/confirm'
import { t, tpl } from '@/lib/i18n'
import { usePoll } from '@/lib/poll'
import { refreshScrewHistory, screwHistory } from '@/lib/screw-history'
import { SCREW_SESSION_TIMEOUT_MIN } from '@/lib/screws'
import { liveNumber } from '@/lib/temp-values'
import { consoleStatus, nav, openPage, printer, refreshConsole, refreshPrinter } from '@/lib/state'

// While a screw session runs the bed stays hot and the nozzle waits for the next probe. The session ends by itself
// after SCREW_SESSION_TIMEOUT_MIN minutes without an action. The bar says so on every other page of CC2 Control, and the
// browser asks before the tab is closed. The page knows the session from /api/printer screws_session.active, which the
// program reports itself (it also turns false when the session ends for any other reason).
const SessionBarBody = ({ onAlign }: { onAlign: boolean }) => {
  const data = printer.use().data
  const history = screwHistory.use()
  const [busy, setBusy] = useState(false)
  usePoll(refreshScrewHistory, 15000)
  usePoll(refreshConsole, 5000) // a running measurement is known on every page
  // The same condition as the other screw buttons: nothing while a measurement or another request runs.
  const measuring = consoleStatus.use().busy
  usePoll(() => setBusy(b => b), 15000) // re-render for the remaining time
  const bed = liveNumber(data?.screws_session?.bed)
  // An estimate: the last action of the session (its start or its newest measurement) plus the idle limit.
  // The program does not report the remaining time.
  const running = history.supported ? history.sessions.find(s => s.kind !== 'quick' && s.ended == null) : undefined
  const last = running ? Math.max(running.started, ...running.measurements.map(m => m.time)) : 0
  const minutes = last ? Math.ceil((last + SCREW_SESSION_TIMEOUT_MIN * 60 - Date.now() / 1000) / 60) : 0
  const end = async () => {
    if (busy || !(await ask(t('bed.tram_end_confirm')))) return
    setBusy(true)
    try {
      await control('screws:tram:end')
      await refreshPrinter()
    } catch (e) {
      notify(errText(e), 'error')
    } finally {
      setBusy(false)
    }
  }
  return (
    <div
      role="status"
      data-level-session-bar
      class="sticky top-17 z-20 mb-3 flex flex-wrap items-center gap-x-3 gap-y-2 rounded-lg border border-amber bg-panel px-3 py-2 text-xs text-amber shadow-pop"
    >
      <TriangleAlert size={18} strokeWidth={1.75} class="shrink-0" />
      <span class="min-w-0 flex-1 basis-60 break-words">
        <strong class="font-semibold">{t('bed.lvl_bar_running')}</strong>
        {bed != null && <> · {tpl('bed.lvl_bar_bed', { temp: bed })}</>}
        {/* The limit counts only while nothing runs: during a measurement or a wait it stands still. */}
        {measuring ? (
          <>
            {' '}
            · <span data-session-bar-paused>{t('bed.lvl_bar_paused')}</span>
          </>
        ) : (
          minutes > 0 && <> · {tpl('bed.lvl_bar_left', { minutes })}</>
        )}
      </span>
      <span class="flex flex-wrap gap-2">
        {!onAlign && (
          <Button
            variant="warning"
            class="min-h-9 px-3 text-xs"
            data-session-bar-open
            onClick={() => openPage('bed', 'align')}
          >
            {t('bed.lvl_bar_open')}
          </Button>
        )}
        {/* A measurement of the session is cancelled here (the field at the top shows only measurements outside one). */}
        {measuring && <ScrewCancelButton />}
        <Button
          class="min-h-9 px-3 text-xs"
          data-session-bar-end
          disabled={busy || measuring}
          onClick={() => void end()}
        >
          {t('bed.tram_end')}
        </Button>
      </span>
    </div>
  )
}

export const SessionBar = () => {
  const active = Boolean(printer.use().data?.screws_session?.active)
  const { page, bed } = nav.use()
  // Closing the tab while the session runs: the browser's own question (its text cannot be changed).
  useEffect(() => {
    if (!active) return
    const ask = (e: BeforeUnloadEvent) => {
      e.preventDefault()
      e.returnValue = ''
    }
    addEventListener('beforeunload', ask)
    return () => removeEventListener('beforeunload', ask)
  }, [active])
  if (!active) return null
  return <SessionBarBody onAlign={page === 'bed' && bed === 'align'} />
}
