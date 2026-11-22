import type { ComponentChildren } from 'preact'
import { store } from './store'

// The question: plain text, or a function that is rendered again while the dialog is open (live values, a choice).
export type ConfirmText = string | (() => ComponentChildren)

// `confirmText` names the confirming button (default: the shared "Confirm").
// `details` is long or technical text (printer commands): folded away under `detailsLabel` until the user opens it.
type Request = {
  text: ConfirmText
  danger: boolean
  details?: { label: string; text: string }
  confirmText?: string
  resolve: (ok: boolean) => void
}
export const confirmState = store({ open: null as Request | null })

// Promise-based replacement for window.confirm(), rendered by <ConfirmDialog />.
export const ask = (
  text: ConfirmText,
  danger = false,
  details?: { label: string; text: string },
  confirmText?: string
) => new Promise<boolean>(resolve => confirmState.set({ open: { text, danger, details, confirmText, resolve } }))
