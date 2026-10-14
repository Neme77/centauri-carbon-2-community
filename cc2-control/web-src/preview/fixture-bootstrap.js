(() => {
  const state = window.__CC2_PREVIEW = {
    connected: true,
    status: 1,
    reject: false,
    commands: [],
    fans: { part: 0, aux: 0, box: 0 },
    trays: [0, 1, 2, 3].map(i => ({tray_id:i,status:i===1||i===2?1:0,filament_color:['','#66BB6A','#48A7FA','#F5F5F5'][i],filament_name:['','PETG','PETG','PLA'][i],filament_type:['','PETG','PETG','PLA'][i]})),
    preferences: { language: 'it', theme: 'dark', ...JSON.parse(localStorage.getItem('cc2-fixture-preferences') || '{}') },
  }
  const printer = () => ({
    connected: state.connected, messages: 42, last_message_age: state.connected ? 0 : 60,
    extruder: { temperature: 27.4, target: 0 }, heater_bed: { temperature: 24.1, target: 0 },
    chamber: { temperature: 25 },
    fans: Object.fromEntries(Object.entries(state.fans).map(([key, value]) => [key, value * 2.55])),
    machine: { status: state.status, status_name: state.status === 2 ? 'Printing' : 'Idle', sub_status: 0, reason: 0, progress: 0 },
    print: { enabled: false, filename: '', state: '', current_layer: 0, total_layers: 0, duration: 0, remaining: 0 },
    motion: { x: 52.5, y: 264, z: 4.97, speed: 1500, speed_mode: 1, homed_axes: '' },
    tuning: { speed_percent: 100, flow_percent: 100, live_velocity: 0 },
    z_offset: { value: 0, reference: 0, adjustment: 0, pending: false, timed_out: false },
    recovery: { available: false, reboot_pending: false, error: 'none' },
    hardware: { camera: true, usb: true, light: 1, filament_detection: true, filament_detected: false },
    spools: { enabled: false, revision: 1, questions: [] },
    printer_report: null, printer_error: null, camera_viewer: null,
  })
  const reply = (data, status = 200) => Promise.resolve(new Response(JSON.stringify(data), {
    status, headers: { 'Content-Type': 'application/json' },
  }))
  window.fetch = (input, options = {}) => {
    const raw = typeof input === 'string' ? input : input.url
    let path = new URL(raw, location.href).pathname
    const api = path.indexOf('/api/')
    if (api >= 0) path = path.slice(api)
    const locale = path.match(/\/(?:locales|i18n)\/(\w+)\.json$/)
    if (locale) return reply(window.__CC2_PREVIEW_LOCALES[locale[1]] || window.__CC2_PREVIEW_LOCALES.en)
    if (path === '/api/preferences') {
      if (options.method === 'PUT') {
        Object.assign(state.preferences, JSON.parse(options.body))
        localStorage.setItem('cc2-fixture-preferences', JSON.stringify(state.preferences))
      }
      return reply(state.preferences)
    }
    if (path === '/api/camera/claim') return reply({viewer:String(options.body)})
    if (path === '/api/canvas') return reply({available:true,auto_refill:false,telemetry:{result:{canvas_info:{canvas_list:[{canvas_id:0,connected:1,tray_list:state.trays}]}}}})
    if (path === '/api/printer') return reply(printer())
    if (path === '/api/health') return reply({ service: 'cc2-control', version: '1.2.3', mqtt_connected: state.connected })
    if (path === '/api/setup') return reply({ required: false, configured: true, mqtt_connected: state.connected })
    if (path === '/api/console') return reply({ busy: false, command: '', output: '', completed: false, success: false })
    if (path === '/api/pid') return reply({ busy: false, ready: false, failed: false, heater: '', kp: null, ki: null, kd: null })
    if (path === '/api/material-presets') {
      if (options.method === 'PUT') {
        state.presets = JSON.parse(options.body)
        localStorage.setItem('cc2-fixture-presets', options.body)
        return reply({saved:true})
      }
      return reply(state.presets || JSON.parse(localStorage.getItem('cc2-fixture-presets') || 'null') || [
      { name: 'PLA', nozzle: 200, bed: 60 }, { name: 'PETG', nozzle: 240, bed: 70 },
      { name: 'ABS', nozzle: 250, bed: 100 }, { name: 'ASA', nozzle: 255, bed: 100 },
      { name: 'TPU', nozzle: 220, bed: 50 }, { name: 'PA-CF', nozzle: 285, bed: 100 },
      ])
    }
    if (path === '/api/control' && options.method === 'POST') {
      const action = String(options.body)
      state.commands.push(action)
      if (state.reject || !state.connected) return reply({ error: 'Comando rifiutato nella simulazione' }, 409)
      if (action === 'canvas:reset-all') {
        if (state.status !== 1) return reply({error:'Stampante occupata'},409)
        state.trays.forEach(t=>Object.assign(t,{filament_color:'',filament_name:'',filament_type:'',min_nozzle_temp:0,max_nozzle_temp:0}))
        return reply({accepted:true,simulated:true})
      }
      const fan = action.match(/^fan:(part|aux|box):(\d+)$/)
      if (fan && Number(fan[2]) <= 100) {
        state.fans[fan[1]] = Number(fan[2])
        return reply({ accepted: true, simulated: true })
      }
      return reply({ error: 'Questa anteprima permette solo di provare le ventole' }, 403)
    }
    return reply({})
  }
  if (!location.hash) location.hash = '#control'
  document.addEventListener('DOMContentLoaded', () => {
    const badge = document.createElement('div')
    badge.textContent = 'ANTEPRIMA · dati simulati · Invio applica la percentuale'
    badge.style.cssText = 'position:fixed;bottom:12px;left:50%;transform:translateX(-50%);z-index:1000;max-width:calc(100% - 24px);padding:8px 14px;border:1px solid #17445b;border-radius:8px;background:#052131;color:#8daebf;font:12px system-ui;text-align:center;pointer-events:none'
    document.body.append(badge)
  })
})()
