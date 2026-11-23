import { ChevronRight } from 'lucide-preact'
import { useState } from 'preact/hooks'
import { InfoHint } from '@/components/info-hint'
import { type Key, t, tpl } from '@/lib/i18n'
import {
  cornerOffsets,
  heightNumber,
  type HeightUnit,
  measurementOffsets,
  SCREW_CORNERS,
  type ScrewHistoryMeasurement,
  type ScrewHistorySession,
  type ScrewId,
  sessionRowOffsets,
} from '@/lib/screws'
import { formatLiveValue } from '@/lib/temp-values'

const CORNER_NAME: Record<ScrewId, Key> = {
  FL: 'bed.corner_front_left',
  FR: 'bed.corner_front_right',
  RR: 'bed.corner_rear_right',
  RL: 'bed.corner_rear_left',
}
const CORNER_SHORT: Record<ScrewId, Key> = {
  FL: 'bed.tram_hist_short_fl',
  FR: 'bed.tram_hist_short_fr',
  RR: 'bed.tram_hist_short_rr',
  RL: 'bed.tram_hist_short_rl',
}
// Time, type, bed, nozzle, chamber and the four corners: the width of a full-width row.
const COLUMNS = 9

type Offsets = Record<ScrewId, number | null>

type Row =
  | { type: 'date'; key: string; label: string }
  | {
      type: 'head'
      key: string
      from: number
      to: number | null
      bed: number | null
      oldPoints: boolean
      finals: Offsets
    }
  | {
      type: 'measurement'
      key: string
      m: ScrewHistoryMeasurement
      time: number
      offsets: Offsets
      inSession: boolean
      // The note about the earlier measuring points belongs to the entry; a row shows it when no head row does.
      oldPoints: boolean
      quick: boolean
    }

const pad = (n: number) => String(n).padStart(2, '0')
const rangeEnd = (from: number, to: number, lang: string) =>
  `${dayKey(to) !== dayKey(from) ? `${shortDay(to, lang)} ` : ''}${clockText(to)}`
export const clockText = (unix: number) => {
  if (!unix) return '—'
  const d = new Date(unix * 1000)
  return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`
}
const dayKey = (unix: number) => {
  const d = new Date(unix * 1000)
  return `${d.getFullYear()}-${d.getMonth()}-${d.getDate()}`
}
const dayLabel = (unix: number, lang: string) => {
  try {
    return new Date(unix * 1000).toLocaleDateString(lang, {
      weekday: 'short',
      year: 'numeric',
      month: 'short',
      day: 'numeric',
    })
  } catch {
    return new Date(unix * 1000).toLocaleDateString()
  }
}
const shortDay = (unix: number, lang: string) => {
  try {
    return new Date(unix * 1000).toLocaleDateString(lang, { day: 'numeric', month: 'numeric' })
  } catch {
    return new Date(unix * 1000).toLocaleDateString()
  }
}

// The rows of the table in the order they are shown. A date row comes before the first row of every new day; a
// session of several measurements gets a head row, its measurements follow indented.
export function historyRows(entries: ScrewHistorySession[], variant: 'sessions' | 'quick', lang: string): Row[] {
  const rows: Row[] = []
  let day = ''
  const dateBefore = (unix: number, key: string) => {
    if (!unix) return
    const k = dayKey(unix)
    if (k === day) return
    day = k
    rows.push({ type: 'date', key: `date-${key}`, label: dayLabel(unix, lang) })
  }
  for (const s of entries) {
    if (variant === 'quick') {
      const sample = [...s.measurements].reverse().find(m => m.kind === 'quick')
      if (!sample) continue
      const time = sample.time || s.started
      dateBefore(time, `q${s.started}-${sample.time}`)
      rows.push({
        type: 'measurement',
        key: `q${s.started}-${sample.time}`,
        m: sample,
        time,
        offsets: measurementOffsets(sample),
        inSession: false,
        oldPoints: s.points === 'old',
        quick: true,
      })
      continue
    }
    const offsets = sessionRowOffsets(s)
    const from = s.started || s.measurements[0]?.time || 0
    const withHead = s.measurements.length !== 1
    dateBefore(from, `s${s.started}`)
    if (withHead) {
      const finals: Offsets = { FL: null, FR: null, RR: null, RL: null }
      const series = cornerOffsets(s)
      for (const c of SCREW_CORNERS) finals[c.id] = series[c.id].length ? series[c.id][series[c.id].length - 1] : null
      rows.push({
        type: 'head',
        key: `s${s.started}`,
        from,
        to: s.ended,
        bed: s.measurements.find(m => m.bed != null)?.bed ?? null,
        oldPoints: s.points === 'old',
        finals,
      })
    }
    s.measurements.forEach((m, i) => {
      dateBefore(m.time, `s${s.started}-${i}`)
      rows.push({
        type: 'measurement',
        key: `s${s.started}-${i}`,
        m,
        time: m.time,
        offsets: offsets[i],
        inSession: withHead,
        oldPoints: !withHead && s.points === 'old',
        quick: false,
      })
    })
  }
  return rows
}

const KIND_KEY: Record<ScrewHistoryMeasurement['kind'], Key> = {
  start: 'bed.tram_hist_kind_start',
  probe: 'bed.tram_hist_kind_probe',
  check: 'bed.tram_hist_kind_check',
  quick: 'bed.tram_hist_kind_quick',
}

const Dash = () => <span class="text-muted">—</span>

const CornerAbbr = ({ id }: { id: ScrewId }) => (
  <abbr title={t(CORNER_NAME[id])} class="no-underline" data-screw-corner={id}>
    {t(CORNER_SHORT[id])}
  </abbr>
)

// The heights of one measurement for the detail row: the single probes when they were written down, otherwise the
// heights (both results with 2× 3 probes), in millimetres with three decimals.
const heightsText = (m: ScrewHistoryMeasurement, id: ScrewId, lang: string): string | null => {
  const fmt = (n: number) => formatMm(n, lang)
  const probes = m.probes?.[id]
  if (probes?.length) return probes.map(fmt).join(' · ')
  const list = [m.z[id], m.twice ? m.z2?.[id] : undefined].filter((n): n is number => typeof n === 'number')
  return list.length ? list.map(fmt).join(' · ') : null
}
const formatMm = (n: number, lang: string) => {
  try {
    return new Intl.NumberFormat(lang, {
      minimumFractionDigits: 3,
      maximumFractionDigits: 3,
      useGrouping: false,
    }).format(n)
  } catch {
    return n.toFixed(3)
  }
}

export const ScrewHistoryTable = ({
  entries,
  variant,
  unit,
  lang,
}: {
  entries: ScrewHistorySession[]
  variant: 'sessions' | 'quick'
  unit: HeightUnit
  lang: string
}) => {
  const [open, setOpen] = useState<Record<string, boolean>>({})
  const rows = historyRows(entries, variant, lang)
  const unitText = unit === 'mm' ? 'mm' : 'µm'
  const num = (v: number | null | undefined) => (v == null ? <Dash /> : heightNumber(v, unit, true, lang))
  const temp = (v: number | undefined, digits: 0 | 1) => (v == null ? <Dash /> : formatLiveValue(v, lang, digits))
  const oldHint = <InfoHint text={t('bed.tram_history_old_points')} data-screw-old-points subtle />
  const caption = t(variant === 'quick' ? 'bed.tram_quick_history' : 'bed.tram_history')
  const numTh = 'px-1 md:px-1.5 py-1.5 text-right font-normal'
  const cellNum = 'px-1 md:px-1.5 py-1 text-right align-middle tabular-nums'
  // Two lines per measurement on a phone: the corners move to a second row with their short names.
  const cornersLine = (key: string, offsets: Offsets) => (
    <tr key={key} class="md:hidden" data-screw-row-corners>
      <td colSpan={COLUMNS} class="px-1 md:px-1.5 pb-1.5">
        <span class="mr-1.5 text-muted">{unitText}</span>
        <span class="inline-flex flex-wrap gap-x-3 gap-y-0.5 tabular-nums">
          {SCREW_CORNERS.map(c => (
            <span key={c.id} data-narrow-corner={c.id}>
              <span class="mr-1 text-muted">{t(CORNER_SHORT[c.id])}</span>
              {num(offsets[c.id])}
            </span>
          ))}
        </span>
      </td>
    </tr>
  )
  return (
    <div class="overflow-visible">
      <table class="w-full border-collapse text-xs" data-screw-history-table={variant} aria-label={caption}>
        <thead>
          <tr class="text-left text-muted">
            <th scope="col" class="px-1 md:px-1.5 py-1.5 font-normal">
              {t('bed.tram_hist_col_time')}
            </th>
            <th scope="col" class="px-1 md:px-1.5 py-1.5 font-normal">
              {t('bed.tram_hist_col_kind')}
            </th>
            {(
              [
                ['preheat.bed', '°C'],
                ['common.nozzle', '°C'],
                ['dashboard.chamber', '°C'],
              ] as const
            ).map(([label, u]) => (
              <th key={label} scope="col" class={numTh}>
                {t(label)}
                <span class="block text-[10px]">{u}</span>
              </th>
            ))}
            {SCREW_CORNERS.map(c => (
              <th key={c.id} scope="col" class={`${numTh} max-md:hidden`}>
                {t(CORNER_NAME[c.id])}
                <span class="block text-[10px]">{unitText}</span>
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {rows.map(row => {
            if (row.type === 'date')
              return (
                <tr key={row.key} data-screw-date class="border-t border-edge">
                  <th scope="colgroup" colSpan={COLUMNS} class="px-1 md:px-1.5 pt-2 pb-1 text-left font-semibold">
                    {row.label}
                  </th>
                </tr>
              )
            if (row.type === 'head')
              return [
                <tr key={row.key} data-screw-session data-screw-kind="session" class="border-t border-edge bg-fg/5">
                  <th scope="colgroup" colSpan={5} class="px-1 md:px-1.5 py-1 text-left font-semibold">
                    <span class="inline-flex flex-wrap items-center gap-x-2 gap-y-0.5">
                      <span>{t('bed.tram_hist_session')}</span>
                      <span class="font-normal tabular-nums" data-screw-range>
                        {clockText(row.from)}
                        {row.to ? `–${rangeEnd(row.from, row.to, lang)}` : ''}
                      </span>
                      {row.bed != null && (
                        <span class="font-normal text-muted" data-screw-bed-target>
                          {tpl('bed.tram_hist_bed_target', { temp: row.bed })}
                        </span>
                      )}
                      {row.oldPoints && oldHint}
                    </span>
                  </th>
                  {SCREW_CORNERS.map(c => (
                    <td
                      key={c.id}
                      data-col={c.id}
                      title={t('bed.tram_hist_final')}
                      class={`${cellNum} font-semibold max-md:hidden`}
                    >
                      {num(row.finals[c.id])}
                    </td>
                  ))}
                </tr>,
                <tr key={`${row.key}-n`} class="bg-fg/5 md:hidden">
                  <td colSpan={COLUMNS} class="px-1 md:px-1.5 pb-1.5">
                    <span class="mr-1.5 text-muted">
                      {t('bed.tram_hist_final')} · {unitText}
                    </span>
                    <span class="inline-flex flex-wrap gap-x-3 gap-y-0.5 font-semibold tabular-nums">
                      {SCREW_CORNERS.map(c => (
                        <span key={c.id} data-narrow-corner={c.id}>
                          <span class="mr-1 font-normal text-muted">{t(CORNER_SHORT[c.id])}</span>
                          {num(row.finals[c.id])}
                        </span>
                      ))}
                    </span>
                  </td>
                </tr>,
              ]
            const { m } = row
            const ist = m.ist
            const expanded = Boolean(open[row.key])
            const corner = m.corner
            const hasCornerName = (m.kind === 'quick' || m.kind === 'probe') && corner
            return [
              <tr
                key={row.key}
                data-screw-row
                data-screw-kind={row.quick ? 'quick' : row.inSession ? m.kind : 'session'}
                data-time={row.time}
                class="border-t border-edge"
              >
                <td
                  class={`py-1 align-middle tabular-nums ${row.inSession ? 'pr-1 pl-3 md:pr-1.5 md:pl-4' : 'px-1 md:px-1.5'}`}
                  data-col="time"
                >
                  <button
                    type="button"
                    class="inline-flex min-h-7 items-center gap-1 rounded-sm text-left"
                    aria-expanded={expanded}
                    aria-label={`${clockText(row.time)} · ${t('bed.tram_hist_details')}`}
                    data-screw-toggle
                    onClick={() => setOpen({ ...open, [row.key]: !expanded })}
                  >
                    <ChevronRight
                      class={`size-3.5 shrink-0 text-muted transition-transform ${expanded ? 'rotate-90' : ''}`}
                      aria-hidden="true"
                    />
                    {clockText(row.time)}
                  </button>
                </td>
                <td class="px-1 md:px-1.5 py-1 align-middle break-words" data-col="kind">
                  <span class="inline-flex flex-wrap items-center gap-x-1.5 gap-y-0.5">
                    <span>{t(m.kind === 'quick' && corner ? 'bed.tram_hist_kind_corner' : KIND_KEY[m.kind])}</span>
                    {hasCornerName && corner && <CornerAbbr id={corner} />}
                    {m.twice && (
                      <span
                        class="rounded-sm border border-edge px-1 tabular-nums"
                        title={t('bed.tram_probe_twice')}
                        data-screw-twice
                      >
                        2× 3
                      </span>
                    )}
                    {row.quick && !corner && (
                      <InfoHint text={t('bed.tram_hist_no_pause')} data-hint="no-pause" subtle />
                    )}
                    {row.oldPoints && oldHint}
                  </span>
                </td>
                <td class={cellNum} data-col="bed">
                  {temp(ist?.bed, 1)}
                </td>
                <td class={cellNum} data-col="nozzle">
                  {temp(ist?.nozzle, 1)}
                </td>
                <td class={cellNum} data-col="chamber">
                  {temp(ist?.chamber, 0)}
                </td>
                {SCREW_CORNERS.map(c => (
                  <td key={c.id} class={`${cellNum} max-md:hidden`} data-col={c.id}>
                    {num(row.offsets[c.id])}
                  </td>
                ))}
              </tr>,
              cornersLine(`${row.key}-n`, row.offsets),
              expanded && (
                <tr key={`${row.key}-d`} data-screw-detail class="bg-fg/5">
                  <td colSpan={COLUMNS} class="px-1 md:px-1.5 py-1.5 text-muted">
                    <div class="flex flex-col gap-0.5">
                      {m.bed != null && <span>{tpl('bed.tram_hist_bed_target', { temp: m.bed })}</span>}
                      {m.twice && <span>{t('bed.tram_probe_twice')}</span>}
                      <span class="font-semibold text-fg">{t('bed.tram_hist_heights')}</span>
                      {SCREW_CORNERS.map(c => {
                        const text = heightsText(m, c.id, lang)
                        if (!text) return null
                        return (
                          <span key={c.id} class="tabular-nums" data-detail-corner={c.id}>
                            {t(CORNER_NAME[c.id])}: {text}
                          </span>
                        )
                      })}
                    </div>
                  </td>
                </tr>
              ),
            ]
          })}
        </tbody>
      </table>
    </div>
  )
}
