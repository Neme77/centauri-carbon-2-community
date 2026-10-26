import { t } from './i18n'

// The printer serves HTTP, where the modern clipboard API may be unavailable.
export async function copyText(text: string) {
  if (navigator.clipboard?.writeText) {
    try {
      await navigator.clipboard.writeText(text)
      return
    } catch {
      // A permission denial can still allow copying during the user click.
    }
  }
  const previous = document.activeElement as HTMLElement | null
  const selection = document.getSelection()
  const ranges = selection
    ? Array.from({ length: selection.rangeCount }, (_, i) => selection.getRangeAt(i).cloneRange())
    : []
  const input = document.createElement('textarea')
  input.value = text
  input.readOnly = true
  input.style.cssText = 'position:fixed;left:-9999px;top:0'
  document.body.append(input)
  try {
    input.select()
    if (!document.execCommand('copy')) throw Error(t('console.copy_failed'))
  } finally {
    input.remove()
    previous?.focus()
    if (selection) {
      selection.removeAllRanges()
      for (const range of ranges) selection.addRange(range)
    }
  }
}
