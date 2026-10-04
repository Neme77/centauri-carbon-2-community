import { i18n } from '@/lib/i18n'
import { menu, nav, printer } from '@/lib/state'
import { PrintWatcher, Sidebar, TitleSync, Toast, Topbar } from '@/components/layout'
import { ConfirmDialog } from '@/components/confirm-dialog'
import { CameraWindow } from '@/components/shared'
import { SetupDialog } from '@/pages/setup'
import { PrintDialog } from '@/pages/print-dialog'
import { Dashboard } from '@/pages/dashboard'
import { Control } from '@/pages/control'
import { Job } from '@/pages/job'
import { Files } from '@/pages/files'
import { Bed } from '@/pages/bed'
import { Canvas } from '@/pages/canvas'
import { Console } from '@/pages/console'
import { Settings } from '@/pages/settings'

const pages = {
  dashboard: Dashboard,
  control: Control,
  job: Job,
  files: Files,
  bed: Bed,
  canvas: Canvas,
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
      <PrintWatcher />
      <Sidebar />
      <Topbar />
      <main
        class={`cc2-main ml-18.5 mt-17 max-w-[2000px] p-3 transition-[margin,opacity] md:p-4 ${collapsed ? '' : 'md:ml-52'} ${stale ? 'opacity-50' : ''}`}
      >
        <div class="cc2-page-content @container/page">
          <Page />
        </div>
      </main>
      <Toast />
      <SetupDialog />
      <PrintDialog />
      <ConfirmDialog />
    </>
  )
}
