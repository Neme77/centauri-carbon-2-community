import type { Key } from '@/lib/i18n'

// An unhomed printer is homed first.
export const homedXYZ = (axes: unknown) => typeof axes === 'string' && ['x', 'y', 'z'].every(a => axes.includes(a))
// The longest hold time the printer service accepts, in minutes (PLATE_SOAK_MAX in src/plates.h; a test compares them).
export const SOAK_MAX_MIN = 60
export const soakOk = (text: string) => /^\d{1,2}$/.test(text.trim()) && Number(text) <= SOAK_MAX_MIN
export const FAILED: Record<string, Key> = {
  homing: 'bed.homing_did_not_finish',
  heating: 'bed.cal_failed_heating',
  busy: 'bed.cal_failed_busy',
  telemetry: 'bed.cal_failed_telemetry',
  probing: 'bed.cal_failed_probing',
}
