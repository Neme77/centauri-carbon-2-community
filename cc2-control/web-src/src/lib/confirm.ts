import { store } from './store'

type Request = { text: string; danger: boolean; resolve: (ok: boolean) => void }
export const confirmState = store({ open: null as Request | null })

// Promise-based replacement for window.confirm(), rendered by <ConfirmDialog />.
export const ask = (text: string, danger = false) =>
  new Promise<boolean>(resolve => confirmState.set({ open: { text, danger, resolve } }))
