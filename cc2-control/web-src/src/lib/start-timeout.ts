// How long the page waits for the answer to a print start. A USB start copies the file and checks it
// before it answers; the slowest whole start measured on the printer took 130 s for 500 MB (about 4 MB/s).
// Half that rate plus 120 s leaves room for a slower stick, and the wait never falls below 300 s. Without a
// known size the wait is 300 s, as before.
export const startTimeoutMs = (bytes?: number | null) =>
  Math.max(300000, 120000 + Math.ceil(((typeof bytes === 'number' && bytes > 0 ? bytes : 0) / 2_000_000) * 1000))
