import { useEffect, useRef, useState } from 'preact/hooks'
import { TriangleAlert } from 'lucide-preact'
import { Button } from '@/components/ui/button'
import { Icon } from '@/components/icons'
import { CalibrationLine, ScrewCancelButton, ScrewMeasureLine, ScrewSoakLine } from '@/components/calibration-bar'
import { errText, notify } from '@/lib/api'
import { ask } from '@/lib/confirm'
import { type Chain, chainText, clearChain, guidedChain, SCREW_JOB, startScrews, stepChain } from '@/lib/guided-chain'
import { t, tpl } from '@/lib/i18n'
import { poll } from '@/lib/poll'
import { dismissUnsavedMesh, noteUnsavedMesh, saveUnsavedMesh, unsavedMesh, unsavedRun } from '@/lib/unsaved-mesh'
import { followLibrary } from '@/lib/level-state'
import { plates, refreshPlates } from '@/lib/plates'
import { SCREW_SETTLE_S } from '@/lib/screws'
import { screwSoakEnd, screwSoakOf } from '@/lib/screw-soak'
import { type BedTab, consoleStatus, nav, openPage, printer, refreshConsole } from '@/lib/state'

// The field at the top of every page of CC2 Control for what runs on the bed outside a screw session: the alignment
// with a new mesh (lib/guided-chain) while a chain is kept, the line of a running mesh calibration – of every mesh, also
// one started in the Mesh or Build plates tab or elsewhere –, the hold time before a screw measurement or a session
// start (lib/screw-soak) and a running screw measurement (quick, one corner). With a chain all stand in ONE field: the
// chain's line on top, under a thin line the rest, a little smaller; the screw points of the chain are said by the
// chain's line alone (with Cancel there). The field drives the chain, so that the screw points follow also when the Bed
// alignment tab is not open. The screw session keeps its own orange bar below (components/session-bar), with its
// measurements.
export const MeshStatus = () => {
  const chain = guidedChain.use()
  const job = plates.use().data?.calibration
  const running = Boolean(job && job.stage !== 'off')
  const kept = Boolean(chain.stage)
  const status = consoleStatus.use()
  const data = printer.use().data
  const session = Boolean(data?.screws_session?.active)
  // The hold time comes first: what the console does meanwhile (the service heats the bed) is said by its line.
  const soak = Boolean(screwSoakOf(data))
  const measuring = status.busy && SCREW_JOB.test(status.command) && !session && !soak
  // A hold this page saw running that ends as 'failed' or 'ended' with a reason: told once (no lasting note).
  const sawSoak = useRef(false)
  useEffect(() => {
    if (soak) sawSoak.current = true
    else if (sawSoak.current && data) {
      const end = screwSoakEnd(data)
      if (!end) return
      sawSoak.current = false
      if ((end.result === 'failed' || end.result === 'ended') && end.reason)
        notify(tpl('bed.soak_ended', { reason: end.reason }), end.result === 'failed' ? 'error' : 'info')
    }
  }, [soak, data])
  // The one source of the library for this field: every 2 s while a calibration runs or a chain follows it (also in a
  // hidden tab while a chain is kept: the calibration takes more than ten minutes, the user may look at another tab
  // meanwhile); otherwise every 15 s, so that a mesh started elsewhere shows up. Pages that read the library join it.
  const fast = running || (kept && chain.stage !== 'hint')
  useEffect(() => poll(refreshPlates, fast ? 2000 : 15000, { hidden: kept }), [fast, kept])
  // The basis of Bed alignment follows the library on every page (a stored final mesh takes over from the working mesh).
  const library = plates.use().data
  useEffect(() => followLibrary(library), [library])
  // A calibration that could not be saved to its plate keeps a note at the top (below).
  useEffect(() => noteUnsavedMesh(library), [library])
  // The console the same way: 2 s while a screw measurement runs, otherwise 20 s (the same slow look as the screw view,
  // so an idle page asks no more often; the chain and the session bar ask more often while they stand, and the
  // scheduler runs one task at the fastest interval asked for).
  useEffect(() => poll(refreshConsole, measuring ? 2000 : 20000), [measuring])
  return (
    <>
      <UnsavedMeshNote />
      {kept ? (
        <ChainBar chain={chain} measuring={measuring} soak={soak} />
      ) : (
        (running || measuring || soak) && (
          <div role="status" data-mesh-status class="mb-3 rounded-lg border border-cyan bg-panel px-3 py-2 shadow-pop">
            {running && <CalibrationLine end={measuring || soak ? undefined : <GoTo tab="mesh" />} />}
            {running && (measuring || soak) && <Divider />}
            {soak && <ScrewSoakLine end={<GoTo tab="align" />} />}
            {measuring && <ScrewMeasureLine end={<GoTo tab="align" />} />}
          </div>
        )
      )}
    </>
  )
}

// The mesh of a calibration on a plate was measured and is in the printer, but was not stored in the plate: the reason,
// a button that stores it now (the request of the Build plates tab) and "Dismiss". Over once the plate holds a newer
// measurement at that temperature and nozzle, or a new calibration starts (lib/unsaved-mesh).
const UnsavedMeshNote = () => {
  const library = plates.use().data
  const { ref } = unsavedMesh.use()
  const [sending, setSending] = useState(false)
  const [error, setError] = useState('')
  const found = ref ? unsavedRun(library, ref) : null
  if (!ref || !found) return null
  const save = async () => {
    if (sending) return
    setSending(true)
    setError('')
    const refused = await saveUnsavedMesh(ref)
    setSending(false)
    if (refused !== null) return setError(refused)
    notify(tpl('bed.measure_saved', { name: found.plate.name, temp: ref.temp }))
  }
  return (
    <div role="status" data-mesh-unsaved class="mb-3 rounded-lg border border-amber bg-panel px-3 py-2 shadow-pop">
      <div class="flex flex-wrap items-center gap-x-3 gap-y-2 text-xs text-amber">
        <TriangleAlert size={18} strokeWidth={1.75} class="shrink-0" />
        <span class="min-w-0 flex-1 basis-60 break-words">
          {tpl('bed.measure_not_saved', { error: found.run.detail })}
        </span>
        <span class="flex flex-wrap gap-2">
          <Button
            variant="primary"
            class="min-h-9 px-3 text-xs"
            data-mesh-unsaved-save
            disabled={sending}
            onClick={() => void save()}
          >
            {t('bed.mesh_save_to_plate')}
          </Button>
          <Button class="min-h-9 px-3 text-xs" data-mesh-unsaved-dismiss onClick={dismissUnsavedMesh}>
            {t('app.dismiss')}
          </Button>
        </span>
      </div>
      {error && (
        <p class="mt-1.5 text-xs break-words text-red" data-mesh-unsaved-error>
          {error}
        </p>
      )}
    </div>
  )
}

// The button of a field that leads back to its tab, in the field's colour; not shown when that tab is open.
const GoTo = ({ tab, warning = false }: { tab: BedTab; warning?: boolean }) => {
  const { page, bed } = nav.use()
  if (page === 'bed' && bed === tab) return null
  return (
    <Button
      variant={warning ? 'warning' : 'accent'}
      class="min-h-9 px-3 text-xs"
      data-mesh-status-open={tab}
      onClick={() => openPage('bed', tab)}
    >
      {t(tab === 'align' ? 'bed.level_tab' : 'bed.mesh_saved_profiles')}
    </Button>
  )
}

const Divider = () => <div class="my-1.5 border-t border-cyan/40" aria-hidden="true" />

const ChainBar = ({ chain, measuring, soak }: { chain: Chain; measuring: boolean; soak: boolean }) => {
  const hint = chain.stage === 'hint'
  // In the hidden tab as well (see above).
  useEffect(() => poll(refreshConsole, 2500, { hidden: true }), [])
  useEffect(() => poll(stepChain, 1000, { hidden: true }), [])
  const [sending, setSending] = useState(false)
  // The note's button: the way of measuring that was confirmed (the quick measurement or the session start) with its
  // bed target and probes, evaluated with the temporary mesh (the basis the chain set).
  const startNow = async () => {
    if (sending) return
    const session = chain.action.startsWith('screws:tram:start')
    const question = tpl(session ? 'bed.tram_start_confirm' : 'bed.start_the_four_screw_load_cell', {
      temp: chain.temp,
      seconds: SCREW_SETTLE_S,
    })
    if (!(await ask(question))) return
    setSending(true)
    try {
      if (await startScrews(chain.action, false)) {
        clearChain()
        openPage('bed', 'align')
      }
    } catch (e) {
      notify(errText(e), 'error')
    } finally {
      setSending(false)
    }
  }
  // Not in the screw stage nor in the note: the mesh of the chain is over (or was never started).
  const calibration = !hint && chain.stage !== 'screws'
  return (
    <div
      role="status"
      data-mesh-status
      data-level-chain-bar={chain.stage}
      data-level-chain-hint={hint ? chain.hint : undefined}
      class={`mb-3 rounded-lg border bg-panel px-3 py-2 shadow-pop ${hint ? 'border-amber' : 'border-cyan'}`}
    >
      <div class={`flex flex-wrap items-center gap-x-3 gap-y-2 text-xs ${hint ? 'text-amber' : 'text-cyan'}`}>
        {hint ? (
          <TriangleAlert size={18} strokeWidth={1.75} class="shrink-0" />
        ) : (
          <Icon n="target" class="size-4.5 shrink-0" />
        )}
        <span class="min-w-0 flex-1 basis-60 break-words">
          <strong class="font-semibold">{t('bed.lvl_chain_title')}</strong> · <span>{chainText(chain)}</span>
        </span>
        <span class="flex flex-wrap gap-2">
          {hint && chain.hint === 'work' && (
            <Button
              variant="primary"
              class="min-h-9 px-3 text-xs"
              data-level-chain-start
              disabled={sending}
              onClick={() => void startNow()}
            >
              {t('bed.lvl_chain_start')}
            </Button>
          )}
          {hint ? (
            <Button class="min-h-9 px-3 text-xs" data-level-chain-dismiss onClick={clearChain}>
              {t('app.dismiss')}
            </Button>
          ) : (
            chain.stage === 'screws' && (
              <Button class="min-h-9 px-3 text-xs" data-level-chain-cancel onClick={clearChain}>
                {t('bed.lvl_chain_cancel')}
              </Button>
            )
          )}
          {/* The screw points of the chain: one statement, the chain's line, with the cancel of the measurement. */}
          {chain.stage === 'screws' && measuring && <ScrewCancelButton />}
          <GoTo tab="align" warning={hint} />
        </span>
      </div>
      {calibration && <CalibrationRow />}
      {/* A hold time or another measurement while the chain's note stands (or before its screw points): below, smaller. */}
      {soak && (
        <>
          <Divider />
          <ScrewSoakLine compact />
        </>
      )}
      {measuring && chain.stage !== 'screws' && (
        <>
          <Divider />
          <ScrewMeasureLine compact />
        </>
      )}
    </div>
  )
}

// The calibration under the chain's line, with the thin line between them; nothing while no calibration runs.
const CalibrationRow = () => {
  const job = plates.use().data?.calibration
  if (!job || job.stage === 'off') return null
  return (
    <>
      <Divider />
      <CalibrationLine compact />
    </>
  )
}
