// A pre-2000 mtime on this printer is not a trustworthy calendar date. Keep
// the original API value untouched: neither browser time nor parsing G-code
// comments can reconstruct when an existing file was uploaded.
export const fileModified = (value: unknown): number | null => {
  const seconds = typeof value === 'number' || typeof value === 'string' ? Number(value) : NaN
  return Number.isFinite(seconds) && seconds >= 946684800 && seconds <= 8640000000000 ? seconds : null
}

export const compareFileDates = (a: { path: string; modified?: unknown }, b: { path: string; modified?: unknown }) =>
  (fileModified(b.modified) ?? 0) - (fileModified(a.modified) ?? 0) || a.path.localeCompare(b.path)
