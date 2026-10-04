import { type Key, t, tpl, tState } from './i18n'

// Codes and meanings below come from ELEGOO's elegoo-link SDK (Apache-2.0), CC2 LAN adapter
// elegoo_fdm_cc2_message_adapter.cpp; see NOTICE.md. Only the numbers' meanings are used.

// machine.sub_status means different things in different states (2075 is "printing" while printing and
// "update failed" while updating), so the table is keyed by machine.status. Codes that only repeat the
// state (2075 while printing, 2901 while auto levelling…) show nothing.
const done: Key[] = ['substate.completed']
const failed: Key[] = ['substate.failed']
const SUB: Record<number, Record<number, Key[]>> = {
  2: {
    1081: ['substate.receiving_file'],
    1082: ['substate.receiving_file'],
    1086: ['substate.receiving_file'],
    1045: ['substate.heating_nozzle'],
    1096: ['substate.heating_nozzle'],
    1405: ['substate.heating_bed'],
    1906: ['substate.heating_bed'],
    2801: ['substate.homing'],
    2802: ['substate.homing'],
    2901: ['substate.auto_leveling'],
    2902: ['substate.auto_leveling'],
    2501: ['substate.pausing'],
    2502: ['substate.paused'],
    2505: ['substate.paused'],
    2401: ['substate.resuming'],
    2077: done,
    2503: ['substate.stopping'],
    2504: ['substate.stopped'],
  },
  3: { 1136: done, 1145: done },
  4: { 1136: done, 1145: done },
  5: { 2902: done },
  6: { 1505: done, 1506: failed },
  7: { 5935: done, 5936: failed },
  8: {
    5934: ['substate.resonance_test'],
    5935: ['substate.resonance_test', ...done],
    5936: ['substate.resonance_test', ...failed],
    1503: ['substate.pid_calibration'],
    1504: ['substate.pid_calibration'],
    1505: ['substate.pid_calibration', ...done],
    1506: ['substate.pid_calibration', ...failed],
    2901: ['substate.auto_leveling'],
    2902: ['substate.auto_leveling', ...done],
  },
  9: { 2074: done, 2075: failed },
  10: { 2802: done, 2803: failed },
  11: { 3001: done },
  13: {
    1061: ['substate.loading_filament'],
    1063: ['substate.loading_filament', ...done],
    1062: ['substate.unloading_filament'],
    1064: ['substate.unloading_filament', ...done],
  },
}

// Translated detail of the /api/printer `machine` block, '' when the state says it all.
export const subState = (machine: any) =>
  (SUB[Number(machine?.status)]?.[Number(machine?.sub_status)] ?? []).map(k => t(k)).join(' · ')

// The machine state followed by its detail, e.g. "Printing · Heating bed".
export const stateText = (v: { state: string; detail: string }) =>
  v.detail ? `${tState(v.state)} · ${v.detail}` : tState(v.state)

// result.error_code of a refused request (convertRequestErrorToElegooError and its documented list).
const ERRORS: Record<number, Key> = {
  109: 'printer.filament_runout',
  1000: 'printer.access_denied',
  1001: 'printer.unsupported_request',
  1002: 'printer.folder_unavailable',
  1003: 'printer.invalid_parameters',
  1004: 'printer.file_write_failed',
  1005: 'printer.token_update_failed',
  1006: 'printer.internal_error',
  1007: 'printer.file_delete_failed',
  1008: 'printer.empty_reply',
  1009: 'printer.busy',
  1010: 'printer.not_printing',
  1011: 'printer.file_copy_failed',
  1012: 'printer.task_not_found',
  1013: 'printer.database_error',
  1021: 'printer.print_file_missing',
  1026: 'printer.bed_mesh_missing',
  9999: 'printer.unknown_error',
}
// The vendor methods CC2 Control sends over MQTT, as the request the user made.
const REQUESTS: Record<number, Key> = {
  1002: 'printer.status_request',
  1019: 'print.enable_timelapse',
  1020: 'printer.print_start',
  1036: 'printer.history_request',
  1038: 'history.clear_history',
  1051: 'printer.timelapse_rendering',
  2004: 'printer.auto_refill_change',
  2005: 'printer.canvas_request',
}

export const printerError = (code: number) => (ERRORS[code] ? t(ERRORS[code]) : tpl('printer.error_n', { code }))
export const printerRequest = (method: number) =>
  REQUESTS[method] ? t(REQUESTS[method]) : tpl('printer.request_n', { method })
