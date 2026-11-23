import type { ComponentChildren } from 'preact'
import { useEffect, useMemo, useRef } from 'preact/hooks'
import { Input, Select } from '@/components/ui/field'
import { Icon } from '@/components/icons'
import { FAILED, SOAK_MAX_MIN, soakOk } from '@/components/calibration-progress'
import { notify } from '@/lib/api'
import { history, refreshHistory } from '@/lib/history'
import { i18n, t, tpl } from '@/lib/i18n'
import { chainTellsCancel } from '@/lib/guided-chain'
import {
  activeMesh,
  adoptMesh,
  adoptWork,
  chooseBasis,
  choosePlain,
  finalOfWork,
  levelActive,
  levelBase,
  levelMesh,
  noteWork,
  refreshActiveMesh,
  setLevelBase,
} from '@/lib/level-state'
import { gridCoversScrews, type MeshGrid, sameGrid } from '@/lib/mesh-align'
import {
  BASIS_MAX_AGE_DAYS,
  BASIS_MAX_PRINTS,
  BASIS_MAX_TEMP_C,
  BASIS_MIN_TEMP_C,
  type BasisChoice,
  choiceValue,
  type MeshAge,
  meshAge,
  parseChoice,
  parseTemp,
} from '@/lib/mesh-basis'
import { usePoll } from '@/lib/poll'
import {
  calibrationForm,
  findMeasure,
  nozzleName,
  type Plate,
  type PlateMesh,
  plates,
  refreshPlates,
  type Measure,
  workMeasure,
} from '@/lib/plates'
import { presets, printer } from '@/lib/state'
import { NumMark } from '@/components/num-mark'
import { useAlignConfig } from '@/pages/bed-align'

// Step 1 of the Bed alignment tab: basis and measurement. With "With mesh" ticked the basis is a saved mesh of a build
// plate, the temporary mesh or the printer's active mesh (then step 2 takes the plate shape off), or the temperature of a
// new mesh; without the tick it is only a bed temperature, from a material or typed in. The active mesh carries no
// plate, temperature or nozzle: its bed temperature is chosen as without the tick. Nothing here blocks a measurement.

type Material = { name: string; bed: number }
const materialsOf = (list: unknown[]): Material[] =>
  (list as Material[]).filter(
    p => p && typeof p.name === 'string' && Number.isFinite(Number(p.bed)) && Number(p.bed) >= 0
  )

export type Basis = {
  // "With mesh" is ticked: the choice is the one with a mesh (levelBase choice), otherwise the temperature (plain).
  meshOn: boolean
  choice: BasisChoice
  // The chosen saved mesh (or the working mesh, id 'work') with its plate; null for a material or a typed temperature,
  // and for the printer's active mesh, of which no plate, temperature, nozzle or date is known.
  found: { plate: Plate; measure: Measure } | null
  // The mesh step 2 takes the plate shape from: the one of `found`, or the printer's active mesh. Null without a mesh.
  mesh: PlateMesh | null
  // The printer's active mesh while it is offered as a basis: not one of the saved meshes or the temporary mesh (that
  // entry is offered instead, with what it knows). After a measurement with it, the copy taken at its start (lib/level-
  // state takeActiveCopy). Null when the printer applies no mesh and there is no copy.
  active: PlateMesh | null
  // The active mesh reaches all four screws (an adaptive mesh of a print may cover only part of the bed).
  activeCovers: boolean
  // The choice the bed temperature comes from: the choice itself, for the active mesh the one without a mesh.
  tempChoice: BasisChoice
  age: MeshAge | null
  // The bed temperature of the measurement as whole degrees, null when it is not a valid value.
  temp: number | null
  tempText: string
  materials: Material[]
}

const plateMesh = (g: MeshGrid): PlateMesh => ({ ...g, x_count: g.points[0]?.length ?? 0, y_count: g.points.length })

// The state of step 1 for the whole tab: the saved choice (with a sensible first one), the age of the chosen mesh
// and the end of a mesh measurement started from the dialog (the new mesh becomes the basis).
export const useBasis = (): Basis => {
  const library = plates.use().data
  const saved = levelBase.use()
  const meshOn = levelMesh.use().on
  const tasks = history.use()
  const materials = materialsOf(presets.use().list)
  const job = library?.calibration
  usePoll(refreshPlates, job && job.stage !== 'off' ? 2000 : 30000)
  usePoll(refreshHistory, 60000) // reads what CC2 Control holds, never asks the printer
  useEffect(() => {
    void refreshPlates()
    void refreshHistory()
  }, [])
  const withMesh = parseChoice(saved.choice)
  const meshFound =
    withMesh.kind === 'mesh'
      ? findMeasure(library, withMesh.id)
      : withMesh.kind === 'work'
        ? workMeasure(library)
        : null
  const plain = parseChoice(saved.plain)
  // The printer's active mesh: read when the tab opens and whenever the printer's phase changes (a print, a mesh measured
  // at the touchscreen or here), as on the Mesh tab, never on every status poll. While a screw session runs it is not read
  // again (only once when the page opens during one): the session's probing changes the phase, and its evaluation keeps
  // the mesh it started with.
  const { config } = useAlignConfig()
  const live = activeMesh.use()
  const status = printer.use().data
  const inSession = Boolean(status?.screws_session?.active)
  const phase = `${status?.machine?.status}/${status?.machine?.sub_status}/${status?.print?.uuid}`
  useEffect(() => {
    if (!inSession || !activeMesh.get().read) void refreshActiveMesh()
  }, [phase, inSession])
  // The active mesh as the same mesh as the temporary one or a saved one (same area, same heights): that entry is
  // offered instead, since it knows plate, temperature and nozzle.
  const work = workMeasure(library)
  const twin: BasisChoice | null = useMemo(() => {
    const g = live.grid
    if (!g) return null
    if (work && sameGrid(g, work.measure.mesh)) return { kind: 'work' }
    for (const p of library?.available ? library.plates : [])
      for (const m of p.measures) if (sameGrid(g, m.mesh)) return { kind: 'mesh', id: m.id }
    return null
  }, [live.grid, library])
  // The copy taken at the start of the last measurement with the active mesh (lib/level-state takeActiveCopy): the basis
  // while it exists, also when the printer no longer applies a mesh (it unloads it during a screw measurement) or applies
  // another one since; the next measurement takes the mesh applied then.
  const copy = levelActive.use().copy
  const liveIsCopy = Boolean(copy && live.grid && sameGrid(copy.grid, live.grid))
  // A saved or temporary twin takes over only while no other copy is the basis.
  const twinTakes = twin && (!copy || liveIsCopy) ? twin : null
  const activeGrid = twinTakes ? null : (copy?.grid ?? (live.grid && !twin ? live.grid : null))
  const active = activeGrid ? plateMesh(activeGrid) : null
  // First start, or the saved mesh is gone: only a temperature (PLA, else the first material), no plate. Whoever just
  // wants to measure the screws needs no mesh; a plate is chosen on purpose. A working mesh that is gone because its
  // final mesh was measured (also while this tab was not open) hands over to that final mesh. While another calibration
  // runs the working mesh stays (CC2 Control keeps it until that run has finished its probing), so the choice stays.
  useEffect(() => {
    if (!library) return
    const start = materials.find(m => /^pla$/i.test(m.name)) ?? materials[0]
    // The temperature without a mesh: PLA (else the first material) the first time.
    if (!saved.plain) {
      if (start) choosePlain({ kind: 'material', name: start.name }, Math.round(Number(start.bed)))
      else choosePlain({ kind: 'custom' })
    }
    if (saved.choice && (withMesh.kind === 'material' || withMesh.kind === 'custom' || meshFound)) return
    // The active mesh: kept while the printer has not answered yet, still applies it or its copy exists; the same mesh
    // as a saved or the temporary mesh hands over to that entry.
    if (withMesh.kind === 'active') {
      if (twinTakes) {
        const m = twinTakes.kind === 'mesh' ? findMeasure(library, twinTakes.id) : work
        return chooseBasis(twinTakes, m?.measure.temp)
      }
      if (active || !live.read) return
    }
    // Gone while a calibration still runs (its probing has ended, it is saving): the choice waits for the end of that
    // run (its own end handling below).
    if (withMesh.kind === 'work' && job && job.stage !== 'off') return
    const final = withMesh.kind === 'work' ? finalOfWork(library) : null
    if (final) return chooseBasis({ kind: 'mesh', id: final.id }, final.temp)
    if (start) chooseBasis({ kind: 'material', name: start.name }, Math.round(Number(start.bed)))
    else chooseBasis({ kind: 'custom' })
  }, [library, saved.choice, saved.plain, meshFound, materials.length, twinTakes, Boolean(active), live.read])
  // After the choice above: the working mesh CC2 Control reports is remembered in this browser (lib/level-state); one
  // whose final mesh is stored, or whose plate is gone, is forgotten.
  useEffect(() => noteWork(library), [library])
  // The end of a measurement started here: say how it went; a saved one becomes the basis.
  const seen = useRef<string | null>(null)
  useEffect(() => {
    if (!job) return
    const before = seen.current
    seen.current = job.stage
    if (before === null || before === 'off' || job.stage !== 'off') return
    void refreshActiveMesh() // the printer applies the mesh just measured
    const plate = library?.plates.find(p => p.id === job.plate)
    if (job.result === 'saved') {
      notify(tpl('bed.measure_saved', { name: plate?.name || '', temp: job.temp }))
      void adoptMesh(job.measure)
    } else if (job.result === 'work') {
      notify(tpl('bed.work_measured', { name: plate?.name || '', temp: job.temp }))
      void adoptWork()
    } else if (job.result === 'done') notify(t('bed.cal_done'))
    // The stop of the guided chain's mesh is told by the chain (on every page), not twice.
    else if (job.result === 'cancelled' && !chainTellsCancel()) notify(t('bed.cal_cancelled'))
    // A run with a plate that was measured but not saved has the note at the top (components/guided-chain), not a toast.
    else if (job.result === 'failed' && job.error === 'saving') {
      if (job.work) notify(tpl('bed.work_not_read', { error: job.detail }), 'error')
      else if (!job.plate) notify(tpl('bed.measure_not_saved', { error: job.detail }), 'error')
    } else if (job.result === 'failed') notify(t(FAILED[job.error] || 'bed.cal_failed_probing'), 'error')
  }, [job?.stage])
  const found = meshOn ? meshFound : null
  const age = found
    ? meshAge(found.measure.measured, Date.now() / 1000, tasks.available && tasks.loaded ? tasks.tasks : null)
    : null
  // With the active mesh the bed temperature is the one without a mesh (a material or a typed temperature).
  const onActive = meshOn && withMesh.kind === 'active'
  const tempText = meshOn && !onActive ? saved.temp : saved.plainTemp
  const mesh = found?.measure.mesh ?? (onActive ? active : null)
  return {
    meshOn,
    choice: meshOn ? withMesh : plain,
    found,
    mesh,
    active: meshOn ? active : null,
    activeCovers: Boolean(active && gridCoversScrews(active, config)),
    tempChoice: meshOn && !onActive ? withMesh : plain,
    age,
    temp: parseTemp(tempText),
    tempText,
    materials,
  }
}

const when = (seconds: number) =>
  seconds > 0 ? new Date(seconds * 1000).toLocaleDateString(i18n.get().lang) : t('bed.lvl_date_unknown')

// One line for a saved mesh: plate · temperature · nozzle · date.
const meshLabel = (library: Parameters<typeof nozzleName>[0], plate: Plate, m: Measure) =>
  [plate.name, `${m.temp} °C`, nozzleName(library, m.nozzle), when(m.measured)].filter(Boolean).join(' · ')

// The age of the chosen mesh while it is within the limits. An older mesh, or one without a date, is told under the
// options of the measurement, beside the start, where it matters.
const AgeLine = ({ basis, class: extra = '' }: { basis: Basis; class?: string }) => {
  const { age } = basis
  if (age?.state !== 'ok') return null
  return (
    <div
      class={`${extra} flex items-center gap-3 rounded-md border border-edge bg-field/60 px-3 py-2 text-xs text-muted`}
      data-level-age={age.state}
    >
      <Icon n="info" class="size-4.5 text-cyan" />
      <span class="shrink-0 leading-tight font-semibold" data-level-counter>
        <span class="block">{tpl('bed.lvl_days', { n: age.days ?? 0 })}</span>
        {age.prints !== null && <span class="block">{tpl('bed.lvl_prints', { n: age.prints })}</span>}
      </span>
      <span class="min-w-0 flex-1 break-words">
        {tpl('bed.lvl_mesh_ok', { days: BASIS_MAX_AGE_DAYS, prints: BASIS_MAX_PRINTS })}
      </span>
    </div>
  )
}

// The hold time before a mesh is probed, in step 1 beside the temperature: the same list and the same setting as in the
// Mesh tab (calibrationForm.soak), 0 to SOAK_MAX_MIN minutes, so nothing chosen here is refused later.
const SoakField = ({ locked }: { locked?: boolean }) => {
  const { soak } = calibrationForm.use()
  return (
    <label class="grid w-48 max-w-full gap-1 text-xs text-muted">
      {t('bed.soak_minutes')}
      <Select
        value={soakOk(soak) ? soak : '0'}
        disabled={locked}
        data-level-soak
        onChange={e => calibrationForm.set({ soak: e.currentTarget.value })}
      >
        {Array.from({ length: SOAK_MAX_MIN + 1 }, (_, minutes) => (
          <option key={minutes} value={String(minutes)}>
            {minutes}
          </option>
        ))}
      </Select>
    </label>
  )
}

export const BasisStep = ({
  basis,
  locked,
  frozen,
  head,
  children,
}: {
  basis: Basis
  locked?: boolean
  // A session is active or a screw measurement runs: the basis cannot be changed.
  frozen?: boolean
  // The way of measuring and "With mesh", above the choice of the basis.
  head?: ComponentChildren
  children?: ComponentChildren
}) => {
  const library = plates.use().data
  const work = workMeasure(library)
  const measures = useMemo(
    () =>
      (library?.available ? library.plates : []).flatMap(p =>
        p.measures
          .slice()
          .sort((a, b) => b.measured - a.measured)
          .map(m => ({ plate: p, measure: m }))
      ),
    [library]
  )
  const value = choiceValue(basis.choice)
  const onChoose = (v: string) => {
    const next = parseChoice(v)
    if (!basis.meshOn) {
      const m = next.kind === 'material' ? basis.materials.find(x => x.name === next.name) : null
      return choosePlain(next, m ? Math.round(Number(m.bed)) : null)
    }
    if (next.kind === 'mesh') {
      const m = findMeasure(library, next.id)
      chooseBasis(next, m?.measure.temp)
    } else if (next.kind === 'work') chooseBasis(next, work?.measure.temp)
    else if (next.kind === 'active') chooseBasis(next)
    else if (next.kind === 'material') {
      const m = basis.materials.find(x => x.name === next.name)
      chooseBasis(next, m ? Math.round(Number(m.bed)) : null)
    } else chooseBasis(next)
  }
  // The bed temperature of the active mesh: a material or a typed temperature, the choice kept for "without a mesh".
  const onChooseTemp = (v: string) => {
    const next = parseChoice(v)
    const m = next.kind === 'material' ? basis.materials.find(x => x.name === next.name) : null
    choosePlain(next, m ? Math.round(Number(m.bed)) : null)
  }
  const onActive = basis.meshOn && basis.choice.kind === 'active'
  const materialOptions = basis.materials.length > 0 && (
    <optgroup label={t('bed.lvl_group_materials')}>
      {basis.materials.map(m => (
        <option key={m.name} value={`material:${m.name}`}>
          {m.name} · {Math.round(Number(m.bed))} °C
        </option>
      ))}
    </optgroup>
  )
  return (
    <section class="mt-3.5 rounded-lg border border-edge" data-level-step="1">
      {/* Variant: same head and inner spacing as steps 2 and 3. */}
      <div class="flex min-h-12 items-center gap-x-3 px-3 py-2">
        <NumMark n={1} />
        <h3 class="min-w-0 flex-1 text-base font-semibold">{t('bed.lvl_s1')}</h3>
      </div>
      <div class="px-3 pb-3">
        {head}
        {!basis.meshOn ? null : basis.choice.kind === 'work' && basis.found ? (
          // The working mesh is no saved mesh: say once that it is only temporary.
          <div
            class="mb-2 flex min-h-12 items-center gap-2.5 rounded-md border border-cyan bg-cyan/10 px-3 py-2 text-xs"
            data-level-work-note
          >
            <Icon n="info" class="size-4.5 shrink-0 text-cyan" />
            <span>{t('bed.lvl_work_temporary')}</span>
          </div>
        ) : basis.found ? (
          <AgeLine basis={basis} class="mb-2" />
        ) : onActive && basis.mesh ? (
          // The active mesh carries no plate, temperature, nozzle or date: its bed temperature is chosen below. When it does
          // not reach the screws (an adaptive mesh of a print), it cannot be compared.
          <div
            class="mb-2 flex min-h-12 items-center gap-2.5 rounded-md border border-cyan bg-cyan/10 px-3 py-2 text-xs"
            data-level-active-note
          >
            <Icon n="info" class="size-4.5 shrink-0 text-cyan" />
            <span>
              {t('bed.lvl_active_unknown')}
              {!basis.activeCovers && (
                <span class="mt-1 block text-amber" data-level-active-partial>
                  {t('bed.lvl_mesh_not_usable')}
                </span>
              )}
            </span>
          </div>
        ) : null}
        <label class="grid gap-1 text-xs text-muted">
          {t(basis.meshOn ? 'bed.lvl_base_choice' : 'bed.lvl_temp_choice')}
          <Select value={value} disabled={frozen} onChange={e => onChoose(e.currentTarget.value)} data-level-base>
            {/* Also while the printer has not answered yet, so a kept choice of it shows as chosen. */}
            {(basis.active || onActive) && <option value="active">{t('plates.mesh_active')}</option>}
            {basis.meshOn && work && (
              <optgroup label={t('bed.lvl_group_work')}>
                <option value="work">{meshLabel(library, work.plate, work.measure)}</option>
              </optgroup>
            )}
            {basis.meshOn && measures.length > 0 && (
              <optgroup label={t('bed.lvl_group_meshes')}>
                {measures.map(({ plate, measure }) => (
                  <option key={measure.id} value={`measure:${measure.id}`}>
                    {meshLabel(library, plate, measure)}
                  </option>
                ))}
              </optgroup>
            )}
            {materialOptions}
            <option value="custom">{t('bed.lvl_custom_temp')}</option>
          </Select>
        </label>
        {onActive && (
          <label class="mt-2 grid gap-1 text-xs text-muted">
            {t('bed.lvl_temp_choice')}
            <Select
              value={choiceValue(basis.tempChoice)}
              disabled={frozen}
              onChange={e => onChooseTemp(e.currentTarget.value)}
              data-level-active-temp
            >
              {materialOptions}
              <option value="custom">{t('bed.lvl_custom_temp')}</option>
            </Select>
          </label>
        )}
        <div class="mt-2 flex flex-wrap items-end gap-x-3 gap-y-2">
          {basis.tempChoice.kind === 'custom' && (
            <label class="grid w-48 max-w-full gap-1 text-xs text-muted">
              {t('common.bed_temperature_c')}
              <Input
                type="number"
                inputMode="numeric"
                min={BASIS_MIN_TEMP_C}
                max={BASIS_MAX_TEMP_C}
                step="1"
                value={basis.tempText}
                disabled={frozen}
                aria-invalid={basis.temp == null}
                onInput={e =>
                  setLevelBase(
                    basis.meshOn && !onActive ? { temp: e.currentTarget.value } : { plainTemp: e.currentTarget.value }
                  )
                }
                data-level-custom-temp
              />
            </label>
          )}
          <SoakField locked={locked} />
        </div>
        {basis.temp == null && (
          <p class="mt-1 text-xs text-red" role="alert" data-level-temp-invalid>
            {t('common.invalid_bed_temperature')}
          </p>
        )}
        {children}
      </div>
    </section>
  )
}
