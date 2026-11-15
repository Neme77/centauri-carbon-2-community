// How long the page waits for the answer to a print start. A USB start copies the file and checks it
// before it answers. Assumption until the current copy is measured on the printer: the stick reads at
// 4 MB/s or more and is read twice (check, then copy), so 1 s per 2 MB plus 120 s for the rest. The wait
// never falls below 300 s and never exceeds START_TIMEOUT_MAX_MS: a hung stick or a huge file does not
// keep the page waiting longer, and the page then says so (print.start_no_answer). Without a known size
// the wait is 300 s, as before.
export const START_TIMEOUT_MAX_MS = 1_200_000
export const startTimeoutMs = (bytes?: number | null) =>
  Math.min(
    START_TIMEOUT_MAX_MS,
    Math.max(300000, 120000 + Math.ceil(((typeof bytes === 'number' && bytes > 0 ? bytes : 0) / 2_000_000) * 1000))
  )
