import { PrintCompleted } from '@/components/print-completed'
import { i18n } from '@/lib/i18n'
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
  const stale = !ok || (data && !data.connected) // last known values stay visible, but dimmed
  const Page = pages[page]
  if (camera) return <CameraWindow />
  return (
    <>
      <TitleSync />
      <PrintCompleted />
      <RefusalWatcher />
      <Sidebar />
      <Topbar />
      <main
        tabIndex={-1}
        class={`cc2-main ml-15.5 mt-17 p-3 transition-[margin,padding,opacity] md:p-4 ${collapsed ? 'cc2-main-collapsed' : 'md:ml-52'} ${stale ? 'opacity-50' : ''}`}
      >
        <div class="cc2-page-content @container/page">
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
