import { request } from '@/lib/api'
import { parseScrewHistory, type ScrewHistorySession } from '@/lib/screws'
import { store } from '@/lib/store'

// The screw history of CC2 Control, read by the Screws tab and by the quick check of Mesh alignment.
export const screwHistory = store({
  supported: null as boolean | null,
  sessions: [] as ScrewHistorySession[],
  raw: null as unknown,
})

export async function refreshScrewHistory() {
  if (screwHistory.get().supported === false) return
  try {
    const data = await request('/api/screws/history')
    screwHistory.set({ supported: true, sessions: parseScrewHistory(data), raw: data })
  } catch (e: any) {
    if (e?.status === 404) screwHistory.set({ supported: false, sessions: [], raw: null })
  }
}
