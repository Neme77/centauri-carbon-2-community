import { PrintCompleted } from '@/components/print-completed'
import { ChamberReady } from '@/components/chamber-ready'
import { RunoutContinue } from '@/components/runout-continue'
import { usePoll } from '@/lib/poll'
import { refreshRfidLifecycle, rfidLifecycle, rfidRebootRequired } from '@/lib/rfid-lifecycle'
import { i18n, t } from '@/lib/i18n'
import { menu, nav, printer } from '@/lib/state'
import { PrinterReport, RefusalWatcher, Sidebar, TitleSync, Toast, Topbar } from '@/components/layout'
import { ConfirmDialog } from '@/components/confirm-dialog'
import { CameraWindow, PersistentCamera } from '@/components/shared'
import { SpoolWatcher } from '@/components/spool-ui'
import { SetupDialog } from '@/pages/setup'
import { PrintDialog } from '@/pages/print-dialog'
import { Dashboard } from '@/pages/dashboard'
import { Control } from '@/pages/control'
import { Job } from '@/pages/job'
import { Files } from '@/pages/files'
import { Queue } from '@/pages/queue'
import { History } from '@/pages/history'
import { Bed } from '@/pages/bed'
import { Canvas } from '@/pages/canvas'
import { Spools } from '@/pages/spools'
import { PandaBreath } from '@/pages/pandabreath'
import { Console } from '@/pages/console'
import { Settings } from '@/pages/settings'

const pages = {
  dashboard: Dashboard,
  control: Control,
  job: Job,
  files: Files,
  queue: Queue,
  history: History,
  bed: Bed,
  canvas: Canvas,
  spools: Spools,
  pandabreath: PandaBreath,
  console: Console,
  settings: Settings,
}

export const App = () => {
  i18n.use() // re-render the whole tree when the language changes
  const { page, camera } = nav.use()
  const { collapsed } = menu.use()
  const { data, ok } = printer.use()
  const { data: lifecycle } = rfidLifecycle.use()
  usePoll(refreshRfidLifecycle, 3000)
  const stale = !ok || (data && !data.connected) // last known values stay visible, but dimmed
  const Page = pages[page]
  if (camera) return <CameraWindow />
  return (
    <>
      <TitleSync />
      <PrintCompleted />
      <ChamberReady />
      <RunoutContinue />
      <RefusalWatcher />
      <Sidebar />
      <Topbar />
      <main
        tabIndex={-1}
        class={`cc2-main ml-15.5 mt-17 p-3 transition-[margin,padding,opacity] md:p-4 ${collapsed ? 'cc2-main-collapsed' : 'md:ml-52'}`}
      >
        {rfidRebootRequired(lifecycle) && (
          <p role="status" data-rfid-reboot class="mb-3 rounded-md border border-edge bg-field p-3 text-sm text-amber">
            {t(
              lifecycle?.phase === 4
                ? 'rfid.lifecycle_second_reboot'
                : lifecycle?.phase === 7
                  ? 'rfid.lifecycle_restore_reboot'
                  : 'rfid.lifecycle_reboot'
            )}
          </p>
        )}
        <div class={`cc2-page-content @container/page ${stale ? 'opacity-50' : ''}`}>
          <PrinterReport />
          <Page />
        </div>
      </main>
      <PersistentCamera />
      <Toast />
      <SetupDialog />
      <PrintDialog />
      <SpoolWatcher />
      <ConfirmDialog />
    </>
  )
}
