// Explicit, read-only and opt-in diagnostic export. NEVER serialize raw API
// objects, exception messages, UUIDs, addresses, filenames or LAN secrets.
export type DiagnosticRequest = (path: string) => Promise<unknown>

const object = (value: unknown): Record<string, unknown> =>
  value !== null && typeof value === 'object' && !Array.isArray(value) ? (value as Record<string, unknown>) : {}
const field = (value: unknown, key: string): unknown => object(value)[key]
const flag = (value: unknown): boolean | null => (typeof value === 'boolean' ? value : null)
const number = (value: unknown): number | null =>
  typeof value === 'number' && Number.isFinite(value) && Math.abs(value) <= 1_000_000 ? value : null
const version = (value: unknown): string | null =>
  typeof value === 'string' && /^\d+\.\d+\.\d+$/.test(value) && value.length <= 24 ? value : null

const status = (result: PromiseSettledResult<unknown>): 'ok' | 'unavailable' | 'invalid' =>
  result.status === 'rejected'
    ? 'unavailable'
    : result.value && typeof result.value === 'object' && !Array.isArray(result.value)
      ? 'ok'
      : 'invalid'

// Only fixed keys and checked primitives reach the generated JSON. All
// endpoints are GETs via the existing bounded request helper.
export async function collectDiagnostics(get: DiagnosticRequest) {
  const [health, printer, canvas] = await Promise.allSettled([
    get('/api/health'),
    get('/api/printer'),
    get('/api/canvas'),
  ])
  const h = health.status === 'fulfilled' ? health.value : null
  const p = printer.status === 'fulfilled' ? printer.value : null
  const c = canvas.status === 'fulfilled' ? canvas.value : null
  const printerError = field(p, 'printer_error')
  const telemetry = field(c, 'telemetry') ?? c
  const modules = field(field(field(telemetry, 'result'), 'canvas_info'), 'canvas_list')
  const connected = Array.isArray(modules)
    ? modules.filter(item => field(item, 'connected') === 1 || field(item, 'connected') === true)
    : []
  const trayRecords = connected.reduce((sum: number, item: unknown) => {
    const trays = field(item, 'tray_list')
    return sum + (Array.isArray(trays) ? Math.min(trays.length, 16) : 0)
  }, 0)
  return {
    format: 'cc2-control-diagnostics',
    schema_version: 1,
    created_at: new Date().toISOString(),
    sources: {
      health: status(health),
      printer: status(printer),
      canvas: status(canvas),
    },
    health: {
      version: version(field(h, 'version')),
      uptime_seconds: number(field(h, 'uptime_seconds')),
      memory_total_kb: number(field(h, 'mem_total_kb')),
      memory_available_kb: number(field(h, 'mem_available_kb')),
      mqtt_connected: flag(field(h, 'mqtt_connected')),
      mqtt_registered: flag(field(h, 'mqtt_registered')),
      snapshot_received: flag(field(h, 'snapshot_received')),
    },
    printer: {
      connected: flag(field(p, 'connected')),
      nozzle_temperature_c: number(field(field(p, 'extruder'), 'temperature')),
      bed_temperature_c: number(field(field(p, 'heater_bed'), 'temperature')),
      z_offset_mm: number(field(field(p, 'z_offset'), 'value')),
      z_offset_pending: flag(field(field(p, 'z_offset'), 'pending')),
      last_error_method: number(field(printerError, 'method')),
      last_error_code: number(field(printerError, 'code')),
      last_error_age_seconds: number(field(printerError, 'age')),
    },
    canvas: {
      auto_refill: flag(field(c, 'auto_refill')),
      connected_modules: Array.isArray(modules) ? connected.length : null,
      slot_records: Array.isArray(modules) ? trayRecords : null,
    },
  }
}
