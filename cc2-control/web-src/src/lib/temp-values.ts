// Three kinds of temperature value, kept apart wherever a measurement shows them:
// - the profile value: the material profile's own number, written as the language writes numbers (62,5 / 62.5);
// - the value it is measured at: whole degrees as the backend takes them, with "~" only when rounded;
// - the live value: what the printer reports now, one decimal (the chamber sensor reads whole degrees),
//   shown as no longer current once the last printer status is older than LIVE_STALE_MS.

export const LIVE_STALE_MS = 10_000

export const formatProfileValue = (value: number, lang: string) =>
  new Intl.NumberFormat(lang, { maximumFractionDigits: 3 }).format(value)

export const formatTargetValue = (value: number, approx: boolean) => `${approx ? '~' : ''}${Math.round(value)}`

export const formatLiveValue = (value: number, lang: string, digits: 0 | 1) =>
  new Intl.NumberFormat(lang, { minimumFractionDigits: digits, maximumFractionDigits: digits }).format(value)

// The live value is current only while the last printer status arrived and is at most LIVE_STALE_MS old
// (at and now on the same clock: performance.now(), as refreshPrinter stamps it).
export const liveStale = (at: number, ok: boolean, now: number) => !ok || !at || now - at > LIVE_STALE_MS

export const liveNumber = (value: unknown): number | null => {
  if (value === null || value === undefined || value === '') return null
  const n = Number(value)
  return Number.isFinite(n) ? n : null
}
