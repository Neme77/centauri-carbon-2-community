import { t, tpl } from './i18n'
import { copyEstimate } from './printer-file-limit'

// The question before a start (or a queued job) of a file of 2 GiB or more: why it may not print, for a USB file how
// long the copy takes and how much internal memory it takes (estimated), then cancel or go on.
export const largeFileWarning = (storage: string, size: number | null) => {
  const parts = [t('files.not_printable_2gib')]
  if (storage === 'usb' && typeof size === 'number') parts.push(tpl('files.large_copy_estimate', copyEstimate(size)))
  parts.push(t('files.large_not_opened'))
  return `${parts.join(' ')}\n\n${t('files.print_anyway')}`
}
