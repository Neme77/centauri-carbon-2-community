import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import type { Plugin } from 'vite'

// biome-ignore lint/suspicious/noTemplateCurlyInString: Match the source literally; never evaluate its template.
const cameraSource = "export const camera = (q = '') => `http://${location.hostname}:8080/${q}`"
const cameraSvg = `<svg xmlns="http://www.w3.org/2000/svg" width="640" height="360" viewBox="0 0 640 360"><rect width="640" height="360" fill="#152336"/><path d="M110 280L320 130 530 280 320 330Z" fill="#29445c"/><path d="M280 250V180L320 155 360 180V250L320 275Z" fill="#00cfe8"/><text x="320" y="65" text-anchor="middle" fill="white" font-family="sans-serif" font-size="24">CC2 — DEMO</text></svg>`
// Developer-only translations: never bundled into the firmware UI.
const labels = {
  en: {
    'preview.simulation': 'SIMULATION — no printer',
    'preview.idle': 'Idle',
    'preview.printing': 'Printing',
    'preview.paused': 'Paused',
    'preview.disconnected': 'Disconnected',
    'preview.reset': 'Reset',
    'preview.state': 'State',
  },
  it: {
    'preview.simulation': 'SIMULAZIONE — nessuna stampante',
    'preview.idle': 'Inattiva',
    'preview.printing': 'Stampa',
    'preview.paused': 'Pausa',
    'preview.disconnected': 'Disconnessa',
    'preview.reset': 'Ripristina',
    'preview.state': 'Stato',
  },
  fr: {
    'preview.simulation': 'SIMULATION — aucune imprimante',
    'preview.idle': 'Inactive',
    'preview.printing': 'Impression',
    'preview.paused': 'Pause',
    'preview.disconnected': 'Déconnectée',
    'preview.reset': 'Réinitialiser',
    'preview.state': 'État',
  },
  zh: {
    'preview.simulation': '模拟 — 未连接打印机',
    'preview.idle': '空闲',
    'preview.printing': '打印中',
    'preview.paused': '已暂停',
    'preview.disconnected': '已断开',
    'preview.reset': '重置',
    'preview.state': '状态',
  },
  ru: {
    'preview.simulation': 'СИМУЛЯЦИЯ — без принтера',
    'preview.idle': 'Простой',
    'preview.printing': 'Печать',
    'preview.paused': 'Пауза',
    'preview.disconnected': 'Нет связи',
    'preview.reset': 'Сброс',
    'preview.state': 'Состояние',
  },
}
const scenarios = ['idle', 'printing', 'paused', 'disconnected'] as const
type Scenario = (typeof scenarios)[number]

export function previewPlugin(): Plugin {
  let scene: Scenario = 'printing'
  let speed = 100
  let flow = 100
  let preferences: Record<string, unknown> = { language: 'it', theme: 'dark' }
  let cameraViewer: string | null = null
  let deleted = new Set<string>()
  let autoRefill = false
  let videos: Record<string, number> = {}
  // Build-plate library: which simulated plate mesh each printer side slot holds, as /api/plates reports it.
  const plateMesh = (tilt: number) => ({
    x_count: 11,
    y_count: 11,
    min_x: 6,
    max_x: 246,
    min_y: 6,
    max_y: 246,
    mesh_x_pps: 3,
    mesh_y_pps: 3,
    algo: 'bicubic',
    tension: 0.2,
    offset: 0,
    points: Array.from({ length: 11 }, (_, y) =>
      Array.from({ length: 11 }, (_, x) => Math.round((0.3 + tilt * (x - 5) * 0.01 - (y - 5) ** 2 * 0.003) * 1e6) / 1e6)
    ),
  })
  type PreviewPlate = { id: string; name: string; side: 'A' | 'B'; z_offset: number; measured: number; tilt: number }
  let plates: PreviewPlate[] = []
  let plateSlots: Record<'A' | 'B', number> = { A: 1, B: 2 }
  let plateCurrent = ''
  let plateResult = ''
  const resetPlates = () => {
    plates = [
      { id: 'a1b2c3d4e5f60718', name: 'Smooth PEI', side: 'A', z_offset: -0.02, measured: 1790800000, tilt: 1 },
      { id: '0f1e2d3c4b5a6978', name: 'Textured PEI', side: 'B', z_offset: 0.01, measured: 1790700000, tilt: 2 },
      { id: '1234567890abcdef', name: 'Cool Plate', side: 'A', z_offset: 0, measured: 1790600000, tilt: 3 },
    ]
    plateSlots = { A: 1, B: 2 }
    plateCurrent = 'a1b2c3d4e5f60718'
    plateResult = ''
  }
  resetPlates()
  const plateLibrary = () => ({
    available: true,
    error: '',
    current: plateCurrent,
    pending: '',
    result: plateResult,
    z_applied: Boolean(plateCurrent),
    slots: { A: plateSlots.A ? 'mesh' : 'empty', B: plateSlots.B ? 'mesh' : 'empty' },
    plates: plates.map(({ tilt, ...p }) => ({ ...p, in_printer: plateSlots[p.side] === tilt, mesh: plateMesh(tilt) })),
  })
  // Spool library as /api/spools reports it; tracking starts off so other previews never see its questions.
  const trayColors = ['#EF5350', '#42A5F5', '#FDD835', '#66BB6A']
  type PreviewSpool = Record<string, any> & { id: string }
  let spoolsOn = false
  let spoolRevision = 1
  let spoolList: PreviewSpool[] = []
  let spoolSlots: { spool: string; last: string; question: string; question_since: number }[] = []
  let spoolLog: Record<string, any>[] = []
  const spool = (id: string, name: string, material: string, color: string, remaining: number, extra = {}) => ({
    id,
    name,
    brand: 'ELEGOO',
    material,
    color,
    diameter: 1.75,
    density: material === 'PETG' ? 1.27 : 1.24,
    net: 1000,
    remaining,
    tare: 150,
    low: 100,
    price: 0,
    note: '',
    created: 1790000000,
    used: 1790890000,
    archived: false,
    ...extra,
  })
  const resetSpools = () => {
    spoolsOn = false
    spoolRevision++
    spoolList = [
      spool('aa00000000000001', 'Red PLA', 'PLA', '#EF5350', 640),
      spool('aa00000000000002', 'Blue PLA', 'PLA', '#42A5F5', 820),
      spool('aa00000000000003', 'Green PLA', 'PLA', '#66BB6A', 95),
      spool('aa00000000000004', 'Black PETG', 'PETG', '#16191D', 1000, { used: 0 }),
      spool('aa00000000000005', 'Old white PLA', 'PLA', '#F5F5F5', 30, { archived: true }),
      // A sealed twin stacks with the first black spool; a brand with fewer than three spools goes to "Other".
      spool('aa00000000000006', 'Black PETG', 'PETG', '#16191D', 1000, { used: 0 }),
      spool('aa00000000000007', 'Sunlu PETG Orange', 'PETG', '#FB8C00', 410, { brand: 'Sunlu' }),
      // No colour in the name: its row is labelled with a colour word.
      spool('aa00000000000008', '', 'PETG', '#7E57C2', 250, { brand: 'Sunlu' }),
    ]
    spoolSlots = ['aa00000000000001', 'aa00000000000002', '', 'aa00000000000003', ''].map(id => ({
      spool: id,
      last: '',
      question: '',
      question_since: 0,
    }))
    spoolLog = [
      {
        time: 1790890000,
        spool: 'aa00000000000001',
        slot: 0,
        kind: 'print',
        grams: -18.4,
        mm: 6170,
        job: 'CC2_Preview_Vase_PETG.gcode',
        result: 'complete',
      },
      { time: 1790000000, spool: 'aa00000000000004', slot: -1, kind: 'new', grams: 1000, mm: 0, job: '', result: '' },
    ]
  }
  resetSpools()
  const spoolLibrary = () => {
    const active = scene === 'printing' || scene === 'paused'
    return {
      available: true,
      error: '',
      enabled: spoolsOn,
      revision: spoolRevision,
      canvas: true,
      active_tray: active ? 0 : -1,
      slots: spoolSlots.map((s, slot) => ({
        slot,
        ...s,
        question_mm: 0,
        runout: false,
        printer:
          slot < 4
            ? {
                status: active && slot === 0 ? 2 : 1,
                type: 'PLA',
                // Slot 2 reports an RFID spool: a brand and a product line besides the type.
                name: slot === 1 ? 'PLA Matte' : 'PLA',
                color: trayColors[slot],
                brand: slot === 1 ? 'ELEGOO' : '',
                code: '',
              }
            : null,
      })),
      job: {
        active,
        file: active ? 'CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode' : '',
        started: 1790899000,
        slots: Array.from({ length: 5 }, (_, slot) => ({
          mm: active && slot === 0 ? 3210 : 0,
          grams: active && slot === 0 ? 9.57 : 0,
          spool: active && slot === 0 ? spoolSlots[0].spool : '',
        })),
      },
      spools: spoolList,
      log: spoolLog,
    }
  }
  const reset = () => {
    speed = 100
    flow = 100
    autoRefill = false
    videos = {}
    deleted = new Set()
    cameraViewer = null
    resetPlates()
    resetSpools()
  }
  // Print history as the printer reports it through /api/history (fixed times keep screenshots stable).
  const job = (
    id: string,
    name: string,
    begin: number,
    minutes: number,
    status: number,
    video: number,
    url: string
  ) => ({
    task_id: id,
    task_name: name,
    begin_time: begin,
    end_time: begin + minutes * 60,
    task_status: status,
    time_lapse_video_status: videos[id] ?? video,
    time_lapse_video_size: (videos[id] ?? video) === 2 ? 4839487 : 0,
    time_lapse_video_duration: (videos[id] ?? video) === 2 ? 12 : 0,
    time_lapse_video_url: url,
  })
  const tasks = () =>
    [
      job(
        'preview-1',
        'CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode',
        1790895600,
        26,
        1,
        2,
        'video/CC2_Preview_Buddha.mp4'
      ),
      job(
        'preview-2',
        'A_very_long_filename_for_testing_mobile_portrait_wrapping_and_desktop_sidebar_proportions.gcode',
        1790809200,
        94,
        1,
        1,
        'picture/A_very_long_filename'
      ),
      job('preview-3', 'Preview_USB.gcode', 1790722800, 12, 2, 0, ''),
      job('preview-4', 'CC2_Preview_Vase_PETG.gcode', 1790636400, 211, 1, 3, 'picture/CC2_Preview_Vase_PETG'),
    ].filter(task => !deleted.has(task.task_id))
  const printer = () => {
    const active = scene === 'printing' || scene === 'paused'
    return {
      connected: scene !== 'disconnected',
      messages: 100,
      last_message_age: scene === 'disconnected' ? 60 : 0,
      extruder: { temperature: active ? 210.2 : 28, target: active ? 210 : 0 },
      heater_bed: { temperature: active ? 60.1 : 27, target: active ? 60 : 0 },
      chamber: { temperature: 29 },
      fans: {
        controller: active ? 255 : 0,
        heater: active ? 255 : 0,
        part: scene === 'printing' ? 153 : 0,
        aux: 0,
        box: 0,
      },
      machine: {
        status: active ? 2 : 1,
        status_name: active ? 'Printing' : 'Idle',
        sub_status: scene === 'paused' ? 2502 : active ? 2075 : 0,
        reason: 0,
        progress: active ? 63 : 0,
      },
      print: {
        enabled: active,
        filename: active ? 'CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode' : '',
        state: active ? scene : '',
        uuid: active ? 'preview-job' : '',
        current_layer: active ? 143 : 0,
        total_layers: active ? 227 : 0,
        duration: active ? 1108 : 0,
        remaining: active ? 480 : 0,
        total_duration: active ? 1187 : 0,
      },
      motion: { x: 128, y: 128, z: active ? 28.6 : 5, speed: 3000, speed_mode: 1, homed_axes: 'xyz' },
      tuning: { speed_percent: speed, flow_percent: flow, live_velocity: scene === 'printing' ? 74.8 : 0 },
      hardware: { camera: true, usb: true, light: 1, filament_detection: true, filament_detected: true },
      printer_error: null,
      camera_viewer: cameraViewer,
      spools: {
        enabled: spoolsOn,
        revision: spoolRevision,
        questions: spoolsOn ? spoolSlots.flatMap((s, slot) => (s.question ? [slot] : [])) : [],
      },
    }
  }
  return {
    name: 'cc2-isolated-ui-preview',
    apply: 'serve',
    enforce: 'pre',
    resolveId(source, importer) {
      if (source === '../../public/locales/en.json' && importer?.split('?')[0].endsWith('/src/lib/i18n.ts'))
        return '\0cc2-preview-en'
    },
    load(id) {
      if (id === '\0cc2-preview-en')
        return `export default ${readFileSync(fileURLToPath(new URL('../public/locales/en.json', import.meta.url)), 'utf8')}`
    },
    transform(code, id) {
      if (id.split('?')[0].endsWith('/src/lib/state.ts')) {
        if (!code.includes(cameraSource)) throw new Error('Preview camera isolation needs updating')
        return code.replace(cameraSource, "export const camera = (q = '') => '/__preview/camera.svg' + q")
      }
    },
    transformIndexHtml() {
      const lang = String(preferences.language) as keyof typeof labels
      const t = (id: keyof typeof labels.en) => (labels[lang] || labels.en)[id]
      return [
        {
          tag: 'aside',
          attrs: {
            id: 'cc2-preview-banner',
            style:
              'position:fixed;bottom:8px;right:8px;max-width:calc(100vw - 16px);pointer-events:none;z-index:99999;background:#152336;color:white;padding:8px;border-radius:6px;display:flex;gap:12px;align-items:center;flex-wrap:wrap;font:13px sans-serif;border-top:2px solid #00cfe8',
          },
          children: `<strong>${t('preview.simulation')}</strong><label for="preview-scene">${t('preview.state')}:</label><select id="preview-scene" style="pointer-events:auto;color:#fff;background:#29445c;border:1px solid #7aa2b8;border-radius:4px;padding:3px 6px">${scenarios.map(s => `<option value="${s}" ${s === scene ? 'selected' : ''}>${t(`preview.${s}`)}</option>`).join('')}</select><button id="preview-reset" type="button" style="pointer-events:auto;color:#fff;background:#29445c;border:1px solid #7aa2b8;border-radius:4px;padding:3px 6px">${t('preview.reset')}</button><script type="module">const change = async (scene) => {const r=await fetch('/__preview/scenario',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({scene})});if(r.ok)location.reload()};document.querySelector('#preview-scene').addEventListener('change',e=>change(e.target.value));document.querySelector('#preview-reset').addEventListener('click',()=>change('printing'));</script>`,
          injectTo: 'body',
        },
      ]
    },
    configureServer(server) {
      if (server.config.server.proxy && Object.keys(server.config.server.proxy).length)
        throw new Error('Preview cannot use a backend proxy')
      const state = readFileSync(fileURLToPath(new URL('../src/lib/state.ts', import.meta.url)), 'utf8')
      if (!state.includes(cameraSource)) throw new Error('Preview camera isolation needs updating')
      server.middlewares.use((req, res, next) => {
        const path = new URL(req.url || '/', 'http://localhost').pathname
        const reply = (data: unknown, status = 200) => {
          res.statusCode = status
          res.setHeader('Content-Type', 'application/json')
          res.setHeader('Cache-Control', 'no-store')
          res.end(JSON.stringify(data))
        }
        if (path === '/__preview/camera.svg') {
          res.setHeader('Content-Type', 'image/svg+xml')
          res.end(cameraSvg)
          return
        }
        if (path.startsWith('/i18n/')) {
          const lang = path.match(/^\/i18n\/(en|it|fr|zh|ru)\.json$/)?.[1]
          if (!lang) return reply({ error: 'Unknown preview locale' }, 404)
          res.setHeader('Content-Type', 'application/json')
          res.end(readFileSync(fileURLToPath(new URL(`../public/locales/${lang}.json`, import.meta.url))))
          return
        }
        if (!path.startsWith('/api/') && !path.startsWith('/__preview/')) return next()
        if (req.method === 'GET') {
          switch (path) {
            case '/__preview/scenario':
              return reply({ scene })
            case '/api/printer':
              return reply(printer())
            case '/api/preferences':
              return reply(preferences)
            case '/api/health':
              return reply({
                service: 'cc2-control-preview',
                version: 'DEMO',
                mode: 'simulation',
                uptime_seconds: 1200,
                mem_total_kb: 111168,
                mem_available_kb: 30424,
                loadavg: '2.48 2.64 2.52',
                mqtt_connected: scene !== 'disconnected',
                mqtt_registered: true,
                snapshot_received: true,
              })
            case '/api/setup':
              // CC2_PREVIEW_SETUP=1 shows the first-run "Connect CC2 Control" dialog.
              return reply({
                configured: true,
                snapshot_received: true,
                required: process.env.CC2_PREVIEW_SETUP === '1',
              })
            case '/api/version':
              return reply({ api: '0.1', server: 'CC2 UI preview', text: 'Simulation' })
            case '/api/material-presets':
              return reply([])
            case '/api/orca/pending-print':
              return reply({ pending: false })
            case '/api/console':
              return reply({
                output: '// CC2 UI preview: no hardware commands are executed.\n// All data is simulated.',
              })
            case '/api/gcode-files':
              return reply({
                internal: {
                  available: true,
                  files: [
                    { path: 'CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode', size: 5190855, modified: 1790899200 },
                    {
                      path: 'A_very_long_filename_for_testing_mobile_portrait_wrapping_and_desktop_sidebar_proportions.gcode',
                      size: 102400,
                      modified: 1790899000,
                    },
                  ],
                },
                usb: { available: true, files: [{ path: 'Preview_USB.gcode', size: 123456, modified: 1790898000 }] },
              })
            case '/api/mesh':
              return reply({
                result: {
                  status: {
                    bed_mesh: {
                      mesh_min: [20, 20],
                      mesh_max: [235, 235],
                      probed_matrix: Array.from({ length: 11 }, (_, y) =>
                        Array.from({ length: 11 }, (_, x) => Math.round((x - y) * 0.02 * 1000) / 1000)
                      ),
                      profiles: {},
                    },
                  },
                },
              })
            case '/api/exclude-objects':
              return reply({
                result: {
                  status: {
                    exclude_object: {
                      objects: [
                        {
                          name: 'Buddha',
                          center: [128, 128],
                          polygon: [
                            [110, 110],
                            [146, 110],
                            [146, 146],
                            [110, 146],
                          ],
                        },
                      ],
                      excluded_objects: [],
                      current_object: 'Buddha',
                    },
                  },
                },
              })
            case '/api/plates':
              return reply(plateLibrary())
            case '/api/spools':
              return reply(spoolLibrary())
            case '/api/history':
              return reply({
                available: true,
                pending: false,
                generating: false,
                age: 1,
                error_code: 0,
                reply: { id: 1036, method: 1036, result: { error_code: 0, history_task_list: tasks() } },
              })
            case '/api/canvas':
              return reply({
                auto_refill: autoRefill,
                result: {
                  canvas_info: {
                    canvas_list: [
                      {
                        connected: 1,
                        tray_list: ['#ef5350', '#42a5f5', '#fdd835', '#66bb6a'].map((color, tray_id) => ({
                          tray_id,
                          color,
                          filament_name: 'PLA',
                          filament_color: color,
                          remaining_percent: 75,
                          material: 'PLA',
                          status: 1,
                        })),
                      },
                    ],
                  },
                },
              })
            default:
              return reply({ error: 'Endpoint unavailable in isolated preview' }, 404)
          }
        }
        if (req.method === 'POST' && path === '/api/gcode-files/metadata') {
          return reply({
            layers: 227,
            estimated_seconds: 1547,
            filament_grams: 12.5,
            nozzle_temperature: 210,
            bed_temperature: 60,
          })
        }
        if (req.method === 'POST' && path === '/api/gcode-files/thumbnail') {
          res.setHeader('Content-Type', 'image/svg+xml')
          res.end(cameraSvg)
          return
        }
        // Only simulated tuning, pause/resume/cancel, preferences, auto refill and video rendering change in-memory state.
        const simulated = [
          '/__preview/scenario',
          '/api/control',
          '/api/preferences',
          '/api/canvas/auto-refill',
          '/api/history/refresh',
          '/api/history/delete',
          '/api/camera/claim',
          '/api/history/timelapse',
          '/api/plates/save',
          '/api/plates/edit',
          '/api/plates/delete',
          '/api/plates/recapture',
          '/api/plates/mount',
          '/api/plates/unmount',
          '/api/spools/enable',
          '/api/spools/save',
          '/api/spools/delete',
          '/api/spools/assign',
          '/api/spools/dismiss',
          '/api/spools/adjust',
          '/__preview/spool-insert',
        ]
        if (!simulated.includes(path)) return reply({ error: 'Operation disabled in isolated preview' }, 403)
        let body = ''
        let oversized = false
        req.on('data', chunk => {
          if (oversized) return
          body += chunk
          if (Buffer.byteLength(body) > 65536) {
            oversized = true
            body = ''
            reply({ error: 'Preview body too large' }, 413)
          }
        })
        req.on('end', () => {
          if (oversized) return
          try {
            if (path === '/__preview/scenario' && req.method === 'POST') {
              const value = JSON.parse(body).scene
              if (!scenarios.includes(value)) return reply({ error: 'Unknown scenario' }, 400)
              scene = value
              reset()
              return reply({ scene })
            }
            if (path === '/api/preferences' && req.method === 'PUT') {
              const value = JSON.parse(body)
              if (value.language && !Object.keys(labels).includes(value.language))
                return reply({ error: 'Unknown language' }, 400)
              preferences = { ...preferences, ...value }
              return reply(preferences)
            }
            if (path === '/api/camera/claim' && req.method === 'POST') {
              const viewer = body.trim()
              if (!/^[0-9a-z-]{8,40}$/.test(viewer)) return reply({ error: 'Invalid camera viewer' }, 400)
              cameraViewer = viewer
              return reply({ viewer })
            }
            if (path === '/api/history/delete' && req.method === 'POST') {
              const ids = body.trim().split('\n')
              if (scene !== 'idle') return reply({ error: 'An idle printer is required' }, 409)
              if (!ids.length || ids.some(id => !tasks().some(task => task.task_id === id)))
                return reply({ error: 'Invalid history entry' }, 400)
              for (const id of ids) deleted.add(id)
              return reply({ accepted: true, method: 1038, count: ids.length, simulated: true }, 202)
            }
            if (path === '/api/canvas/auto-refill' && req.method === 'POST') {
              if (!['on', 'off'].includes(body.trim())) return reply({ error: 'Use on or off' }, 400)
              autoRefill = body.trim() === 'on'
              return reply({ accepted: true, method: 2004, simulated: true }, 202)
            }
            if (path === '/api/history/refresh' && req.method === 'POST')
              return reply({ accepted: true, method: 1036, simulated: true }, 202)
            if (path === '/api/history/timelapse' && req.method === 'POST') {
              const task = tasks().find(x => x.task_id === body.trim())
              if (!task || ![1, 3].includes(task.time_lapse_video_status))
                return reply({ accepted: false, error: 'This print has no time-lapse frames to render' }, 409)
              if (scene !== 'idle')
                return reply({ accepted: false, error: 'Rendering a time-lapse video requires an idle printer' }, 409)
              videos[task.task_id] = 2
              return reply({ accepted: true, method: 1051, simulated: true }, 202)
            }
            if (path.startsWith('/api/plates/') && req.method === 'POST') {
              const lines = body.replace(/\n+$/, '').split('\n')
              const plate = plates.find(p => p.id === lines[0])
              const idleOnly = ['/api/plates/save', '/api/plates/mount', '/api/plates/recapture'].includes(path)
              if (idleOnly && scene !== 'idle') return reply({ ok: false, error: 'The printer must be idle' }, 409)
              if (path === '/api/plates/unmount') {
                plateCurrent = ''
                return reply({ mounted: false })
              }
              if (path === '/api/plates/save') {
                const [side, name, z] = lines as ['A' | 'B', string, string]
                if (plates.some(p => p.name === name))
                  return reply({ ok: false, error: 'A plate with this name already exists' }, 409)
                const id = Array.from({ length: 16 }, () => Math.floor(Math.random() * 16).toString(16)).join('')
                plates.push({ id, name, side, z_offset: Number(z), measured: 1790900000, tilt: plateSlots[side] })
                return reply({ saved: true, id }, 201)
              }
              if (!plate) return reply({ ok: false, error: 'Unknown plate' }, 404)
              if (path === '/api/plates/edit') {
                plate.name = lines[1]
                plate.z_offset = Number(lines[2])
                return reply({ saved: true, applied: plateCurrent === plate.id && scene === 'idle' })
              }
              if (path === '/api/plates/delete') {
                plates = plates.filter(p => p !== plate)
                if (plateCurrent === plate.id) plateCurrent = ''
                return reply({ deleted: true })
              }
              if (path === '/api/plates/recapture') {
                plate.tilt = plateSlots[plate.side]
                return reply({ saved: true })
              }
              if (plateSlots[plate.side] !== plate.tilt && lines[1] !== 'REBOOT')
                return reply(
                  {
                    ok: false,
                    reboot_required: true,
                    error: 'Writing this mesh to the printer needs a printer restart',
                  },
                  409
                )
              const reboot = plateSlots[plate.side] !== plate.tilt
              plateSlots[plate.side] = plate.tilt // the simulated restart is instant
              plateCurrent = plate.id
              plateResult = 'mounted'
              return reply({ mounted: true, reboot }, reboot ? 202 : 200)
            }
            // Spool library: the same key=value lines as spools.h, with only the checks the UI relies on.
            if ((path.startsWith('/api/spools/') || path === '/__preview/spool-insert') && req.method === 'POST') {
              const form = Object.fromEntries(
                body
                  .replace(/\n+$/, '')
                  .split('\n')
                  .filter(Boolean)
                  .map(line => [line.slice(0, line.indexOf('=')), line.slice(line.indexOf('=') + 1)])
              )
              const changed = () => {
                spoolRevision++
                return reply({ ok: true })
              }
              if (path === '/__preview/spool-insert') {
                // Simulates filament inserted into a tray: it asks which spool it is.
                const s = spoolSlots[Number(form.slot)]
                s.last = s.spool || s.last
                s.spool = ''
                s.question = 'inserted'
                s.question_since = Math.floor(Date.now() / 1000)
                return changed()
              }
              if (path === '/api/spools/enable') {
                if (!['on', 'off'].includes(body.trim())) return reply({ ok: false, error: 'Use on or off' }, 400)
                spoolsOn = body.trim() === 'on'
                spoolRevision++
                return reply({ ok: true, enabled: spoolsOn })
              }
              const found = spoolList.find(s => s.id === form.id)
              if (path === '/api/spools/save') {
                if (form.id && !found) return reply({ ok: false, error: 'Unknown spool' }, 404)
                const numbers = ['diameter', 'density', 'net', 'remaining', 'tare', 'low', 'price']
                const fields = Object.fromEntries(
                  Object.entries(form)
                    .filter(([k]) => k !== 'id' && k !== 'slot')
                    .map(([k, v]) => [k, numbers.includes(k) ? Number(v) : k === 'archived' ? v === '1' : v])
                )
                if (found) {
                  Object.assign(found, fields)
                  if (found.archived) for (const s of spoolSlots) if (s.spool === found.id) s.spool = ''
                  spoolRevision++
                  return reply({ ok: true, id: found.id })
                }
                const id = Array.from({ length: 16 }, () => Math.floor(Math.random() * 16).toString(16)).join('')
                const fresh = { ...spool(id, '', 'PLA', '#000000', 1000, { used: 0, created: 1790900000 }), ...fields }
                if (form.remaining === undefined) fresh.remaining = fresh.net
                spoolList.push(fresh)
                if (form.slot !== undefined) {
                  const s = spoolSlots[Number(form.slot)]
                  Object.assign(s, { spool: id, question: '', question_since: 0 })
                }
                spoolRevision++
                return reply({ ok: true, id }, 201)
              }
              if (path === '/api/spools/assign') {
                const s = spoolSlots[Number(form.slot)]
                if (!s) return reply({ ok: false, error: 'Invalid assignment' }, 400)
                if (form.spool && !spoolList.some(x => x.id === form.spool))
                  return reply({ ok: false, error: 'Unknown spool' }, 404)
                for (const other of spoolSlots)
                  if (other !== s && form.spool && other.spool === form.spool) other.spool = ''
                if (s.spool && s.spool !== form.spool) s.last = s.spool
                Object.assign(s, { spool: form.spool, question: '', question_since: 0 })
                return changed()
              }
              if (path === '/api/spools/dismiss') {
                Object.assign(spoolSlots[Number(form.slot)], { question: '', question_since: 0 })
                return changed()
              }
              if (!found) return reply({ ok: false, error: 'Unknown spool' }, 404)
              if (path === '/api/spools/delete') {
                spoolList = spoolList.filter(s => s !== found)
                for (const s of spoolSlots) if (s.spool === found.id) s.spool = ''
                return changed()
              }
              const before = found.remaining
              found.remaining = form.gross !== undefined ? Number(form.gross) - found.tare : Number(form.remaining)
              spoolLog.unshift({
                time: Math.floor(Date.now() / 1000),
                spool: found.id,
                slot: -1,
                kind: 'correction',
                grams: found.remaining - before,
                mm: 0,
                job: '',
                result: form.gross !== undefined ? 'weighed' : 'set',
              })
              spoolRevision++
              return reply({ ok: true, remaining: found.remaining })
            }
            if (path === '/api/control' && req.method === 'POST') {
              const action = body.trim()
              const tuning = action.match(/^tune:(speed|flow):(\d+)$/)
              if (tuning && ['printing', 'paused'].includes(scene)) {
                const n = Number(tuning[2])
                const min = tuning[1] === 'speed' ? 25 : 50
                const max = tuning[1] === 'speed' ? 200 : 150
                if (n < min || n > max) return reply({ error: 'Invalid preview tuning range' }, 400)
                if (tuning[1] === 'speed') speed = n
                else flow = n
                return reply({ accepted: true, simulated: true })
              }
              if (action === 'print:pause' && scene === 'printing') scene = 'paused'
              else if (action === 'print:resume' && scene === 'paused') scene = 'printing'
              else if (action === 'print:cancel' && ['printing', 'paused'].includes(scene)) scene = 'idle'
              else return reply({ error: 'Operation disabled in isolated preview' }, 403)
              return reply({ accepted: true, simulated: true })
            }
            return reply({ error: 'Method unavailable in isolated preview' }, 405)
          } catch {
            return reply({ error: 'Invalid preview request' }, 400)
          }
        })
      })
    },
  }
}
