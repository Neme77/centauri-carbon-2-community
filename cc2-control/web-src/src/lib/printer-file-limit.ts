// The printer program (elegoo_printer, ELEGOO, 32-bit ARM) checks a file with the 32-bit stat(): from 2 GiB on that
// fails and the printer reports the file as missing, so it never prints it. CC2 Control does not block such a file;
// the page only warns, in the list and before the start. The wording is one text: locale key files.not_printable_2gib.
export const PRINTER_FILE_LIMIT = 2147483648

export const tooLargeForPrinter = (size: unknown) => typeof size === 'number' && size >= PRINTER_FILE_LIMIT
