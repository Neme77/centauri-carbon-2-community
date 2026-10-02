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
}
const scenarios = ['idle', 'printing', 'paused', 'disconnected'] as const
type Scenario = (typeof scenarios)[number]

export function previewPlugin(): Plugin {
  let scene: Scenario = 'printing'
  let speed = 100
  let flow = 100
  let preferences: Record<string, unknown> = { language: 'it', theme: 'dark' }
  const reset = () => {
    speed = 100
    flow = 100
  }
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
          const lang = path.match(/^\/i18n\/(en|it|fr|zh)\.json$/)?.[1]
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
              return reply({ configured: true, snapshot_received: true, required: false })
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
            case '/api/canvas':
              return reply({
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
        // Only simulated tuning, pause/resume/cancel and preferences can change in-memory state.
        if (!['/__preview/scenario', '/api/control', '/api/preferences'].includes(path))
          return reply({ error: 'Operation disabled in isolated preview' }, 403)
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
