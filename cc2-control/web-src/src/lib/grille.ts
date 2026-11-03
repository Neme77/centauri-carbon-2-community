import { store } from './store'

// Match the existing grille UI policy; the backend remains authoritative.
export const grilleAllowed = (link: { ok: boolean; data: any }) => {
  const d = link.data
  const native = d?.print?.native?.state
  return Boolean(
    link.ok &&
      d?.connected &&
      d.last_message_age >= 0 &&
      d.last_message_age <= 15 &&
      d.machine?.status === 1 &&
      (native === 'standby' || native === 'complete') &&
      d.extruder?.target === 0 &&
      d.heater_bed?.target === 0 &&
      !d.bed_drying?.active
  )
}

// Shared across shortcuts and the fan panel, including page changes.
export const grilleBusy = store({ busy: false })
