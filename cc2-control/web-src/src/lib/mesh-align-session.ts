import { post, request } from './api'
import { store } from './store'

// The guided loop of the mesh-guided screw alignment (/api/screws/align): a session on CC2 Control whose
// rounds are bed mesh calibrations. Every change is asked twice: first without CONFIRM, which sends
// nothing and returns the printer commands, then with it once the user has agreed to exactly those.
export type AlignSession = {
  active: boolean
  round: number
  rounds: number
  measuring: boolean
  measured: boolean
  side: 'A' | 'B'
  temp: number
  soak: number
  nozzle: string
  idle_left: number
  ended: '' | 'ended' | 'saved' | 'failed' | 'cancelled' | 'timeout' | 'print' | 'estop'
  heaters_off_pending: boolean
  calibration: {
    stage: 'off' | 'homing' | 'heating' | 'soaking' | 'probing' | 'saving'
    remaining: number
    bed: number | null
    result: string
    error: string
    detail: string
  }
}
export type AlignPreview = { confirm_required: true; commands: string[]; [key: string]: unknown }

// `supported`: false once the backend answered 404 (an older CC2 Control); the page then hides the loop.
export const alignSession = store({ data: null as AlignSession | null, supported: null as boolean | null })

export async function refreshAlign() {
  if (alignSession.get().supported === false) return
  try {
    alignSession.set({ data: await request('/api/screws/align'), supported: true })
  } catch (e: any) {
    if (e?.status === 404) alignSession.set({ data: null, supported: false })
  }
}

// Tilt of each completed round of the running session, to tell whether turning the screws helped.
export type AlignRound = { round: number; tilt_x: number; tilt_y: number }
export const alignRounds = store({ rounds: [] as AlignRound[] })

// Returns the reply of the confirmed request, or null when the user declined: then nothing was sent.
export async function alignRequest(
  path: 'measure' | 'save' | 'end',
  lines: string[],
  confirm: (preview: AlignPreview) => Promise<boolean>
) {
  const body = lines.join('\n')
  const preview = await post(`/api/screws/align/${path}`, body)
  // An end without a session answers at once; there is nothing to confirm.
  if (!preview?.confirm_required) return preview
  if (!Array.isArray(preview.commands) || !(await confirm(preview as AlignPreview))) return null
  return post(`/api/screws/align/${path}`, lines.length ? `${body}\nCONFIRM` : 'CONFIRM')
}

export const tiltWorst = (r: { tilt_x: number; tilt_y: number }) => Math.max(Math.abs(r.tilt_x), Math.abs(r.tilt_y))
