// The printer program (elegoo_printer, ELEGOO, 32-bit ARM) checks a file with the 32-bit stat(): from 2 GiB on that
// fails and the printer reports the file as missing, so it has not printed such a file. The operator decides: the
// page warns in the list and before the start, and a start (or a queued job) of such a file carries the confirmation
// line "large_ok"; without it CC2 Control refuses it (reason file_too_large_for_printer). After a confirmed start
// CC2 Control watches whether the printer begins it (status print_watch). The warning: files.not_printable_2gib.
export const PRINTER_FILE_LIMIT = 2147483648
export const LARGE_OK = 'large_ok'

export const tooLargeForPrinter = (size: unknown) => typeof size === 'number' && size >= PRINTER_FILE_LIMIT

// Measured on the printer (run gross-2.5, 16.11.): a 2.5 GiB file (2 684 354 560 bytes) took 434.5 s from the USB
// stick to a checked internal copy. The estimate scales that one run with the size; it is an estimate, not a promise
// (another stick reads at another speed). The copy takes the file's size in internal memory.
export const MEASURED_COPY_BYTES = 2684354560
export const MEASURED_COPY_SECONDS = 434.5
export const copyEstimate = (bytes: number) => ({
  minutes: Math.max(1, Math.round(((bytes / MEASURED_COPY_BYTES) * MEASURED_COPY_SECONDS) / 60)),
  gib: Math.round((bytes / 2 ** 30) * 10) / 10,
})
