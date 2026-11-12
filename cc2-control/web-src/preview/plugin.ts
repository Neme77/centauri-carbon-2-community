import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import type { Plugin } from 'vite'
import { demoExcludeObjects, demoLayerPath } from './layer-path-demo.ts'

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
  de: {
    'preview.simulation': 'SIMULATION — kein Drucker',
    'preview.idle': 'Idle',
    'preview.printing': 'Druckt',
    'preview.paused': 'Pausiert',
    'preview.disconnected': 'Getrennt',
    'preview.reset': 'Zurücksetzen',
    'preview.state': 'Status',
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
  pl: {
    'preview.simulation': 'SYMULACJA — bez drukarki',
    'preview.idle': 'Bezczynna',
    'preview.printing': 'Drukowanie',
    'preview.paused': 'Wstrzymano',
    'preview.disconnected': 'Rozłączona',
    'preview.reset': 'Resetuj',
    'preview.state': 'Stan',
  },
  cs: {
    'preview.simulation': 'SIMULACE — bez tiskárny',
    'preview.idle': 'Nečinná',
    'preview.printing': 'Tisk',
    'preview.paused': 'Pozastaveno',
    'preview.disconnected': 'Odpojeno',
    'preview.reset': 'Obnovit',
    'preview.state': 'Stav',
  },
}
const scenarios = ['idle', 'printing', 'paused', 'disconnected'] as const
type Scenario = (typeof scenarios)[number]

export function previewPlugin(): Plugin {
  let scene: Scenario = 'printing'
  let speed = 100
  let flow = 100
  let tramSession = false
  let tramHeights: Record<string, number> = { '30,30': 1.8, '230,30': 1.85, '230,225': 1.82, '30,225': 1.79 }
  type TramMeasure = {
    time: number
    kind: 'start' | 'probe' | 'check' | 'quick'
    corner: string | null
    twice: boolean
    z: Record<string, number>
    z2?: Record<string, number>
  }
  type TramSession = {
    kind?: 'session' | 'quick'
    started: number
    ended: number | null
    measurements: TramMeasure[]
  }
  const seedScrewHistory = (): TramSession[] => [
    {
      kind: 'quick',
      started: 1762801500,
      ended: 1762801680,
      measurements: [
        {
          time: 1762801680,
          kind: 'quick',
          corner: null,
          twice: false,
          z: { FL: -0.6821, FR: -0.676, RR: -0.8704, RL: -0.751 },
        },
      ],
    },
    {
      started: 1762800000,
      ended: 1762801200,
      measurements: [
        {
          time: 1762800300,
          kind: 'start',
          corner: null,
          twice: false,
          z: { FL: -0.6818, FR: -0.4938, RR: -0.8698, RL: -0.7508 },
        },
        {
          time: 1762800900,
          kind: 'check',
          corner: null,
          twice: false,
          z: { FL: -0.6818, FR: -0.6758, RR: -0.8698, RL: -0.7508 },
        },
      ],
    },
  ]
  let screwSessions: TramSession[] = seedScrewHistory()
  const screwNow = () => Math.floor(Date.now() / 1000)
  const heightsZ = () => ({
    FL: +tramHeights['30,30'].toFixed(4),
    FR: +tramHeights['230,30'].toFixed(4),
    RR: +tramHeights['230,225'].toFixed(4),
    RL: +tramHeights['30,225'].toFixed(4),
  })
  const closeOpenScrew = () => {
    for (const s of screwSessions) if (s.kind !== 'quick' && s.ended == null) s.ended = screwNow()
  }
  const trimScrewHistory = () => {
    let sessions = 0
    let quicks = 0
    screwSessions = screwSessions.filter(s => {
      if (s.kind === 'quick') {
        if (quicks >= 10) return false
        quicks++
        return true
      }
      if (sessions >= 20) return false
      sessions++
      return true
    })
  }
  let consoleJob = {
    busy: false,
    completed: false,
    success: false,
    generation: 0,
    command: '',
    output: '// CC2 UI preview: no hardware commands are executed.\n// All data is simulated.\n',
  }
  const tramCorner = (id: string) =>
    ({
      FL: { key: '30,30', x: 30, y: 30 },
      FR: { key: '230,30', x: 230, y: 30 },
      RR: { key: '230,225', x: 230, y: 225 },
      RL: { key: '30,225', x: 30, y: 225 },
    })[id]
  const probeLines = (key: string, z: number) => {
    const [x, y] = key.split(',')
    return `${`// probe at ${x}.000,${y}.000 is z=${z.toFixed(6)}\n`.repeat(3)}// Result is z=${z.toFixed(6)}\n`
  }
  const tramOutput = (keys: string[]) =>
    `SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\n${keys.map(k => probeLines(k, tramHeights[k])).join('')}`
  const finishTram = (command: string, keys: string[]) => {
    consoleJob = {
      busy: false,
      completed: true,
      success: true,
      generation: consoleJob.generation + 1,
      command,
      output: tramOutput(keys),
    }
  }
  let preferences: Record<string, unknown> = { language: 'en', theme: 'dark' }
  let cameraViewer: string | null = null
  let deleted = new Set<string>()
  let autoRefill = false
  // Canvas pull-back: the printer's configured value and the autosave.cfg override, pending until a restart.
  let unload = { configured: 85, override: null as number | null, pending: false, extended: false }
  let videos: Record<string, number> = {}
  // Canvas trays as /api/canvas reports them. /__preview/canvas-tray empties or refills one, or changes its label.
  const canvasTrays = ['#ef5350', '#42a5f5', '#fdd835', '#66bb6a'].map((color, tray_id) => ({
    tray_id,
    color,
    filament_name: 'PLA',
    filament_color: color,
    remaining_percent: 75,
    material: 'PLA',
    status: 1,
  }))
  // Print queue: jobs prepared ahead. The preview keeps them in memory and a start "prints" a copy at once.
  // A job is bound to filaments, as on the backend: for every tool the filament as a Canvas slot labels it
  // (empty until known) and the slot the job was added with (null: added for a filament alone).
  type QueueLabel = { type: string; name: string; color: string }
  type QueueTool = QueueLabel & { tool: number; slot: number | null }
  const labelKnown = (f: QueueLabel) => Boolean((f.type || f.name) && f.color)
  // The demo's trays report a filament name and a colour, no type.
  const trayLabel = (slot: number): QueueLabel => {
    const tray = canvasTrays[slot]
    return { type: '', name: tray?.filament_name || '', color: (tray?.filament_color || '').toUpperCase() }
  }
  // As queue_filament_in: the same colour, and the type and the name the filament states.
  const trayHolds = (slot: number, f: QueueLabel) => {
    const label = trayLabel(slot)
    return (
      label.color === f.color.toUpperCase() &&
      (!f.type || f.type.toLowerCase() === label.type.toLowerCase()) &&
      (!f.name || f.name.toLowerCase() === label.name.toLowerCase())
    )
  }
  // "tool:slot,...": tools on a chosen slot, their filament not known yet.
  const mappingTools = (mapping: string): QueueTool[] =>
    mapping
      .split(',')
      .filter(Boolean)
      .map(pair => {
        const [tool, slot] = pair.split(':').map(Number)
        return { tool, slot, type: '', name: '', color: '' }
      })
  // "tool<TAB>type<TAB>name<TAB>#RRGGBB" lines: tools bound to a filament by itself. null: a line is not one.
  const filamentTools = (lines: string[]): QueueTool[] | null => {
    const out: QueueTool[] = []
    for (const line of lines) {
      if (!line) continue
      const [tool, type, name, color, more] = line.split('\t')
      if (more !== undefined || !/^\d+$/.test(tool) || !/^#[0-9a-f]{6}$/i.test(color || '') || !(type || name))
        return null
      out.push({ tool: Number(tool), slot: null, type, name, color: color.toUpperCase() })
    }
    return out
  }
  // A tool added with a slot takes the filament the slot is labelled with (queue_note_filaments).
  const noteFilaments = (tools: QueueTool[]) => {
    for (const t of tools) {
      if (t.slot === null || labelKnown(t)) continue
      const label = trayLabel(t.slot)
      if (labelKnown(label)) Object.assign(t, label)
    }
    return tools.sort((a, b) => a.tool - b.tool)
  }
  const mappingText = (tools: QueueTool[]) =>
    tools
      .filter(t => t.slot !== null)
      .map(t => `${t.tool}:${t.slot}`)
      .join(',')
  type QueueJob = {
    id: number
    storage: 'internal' | 'usb'
    path: string
    tools: QueueTool[]
    side: 'A' | 'B'
    leveling: string
    timelapse: boolean
    measure: string
    nozzle: string
    copies: number
    done: number
    estimated_seconds: number | null
    filament_grams: number | null
    added: number
    state: 'pending' | 'starting' | 'printing' | 'done'
    last_result: string
    present: boolean
  }
  let queueJobs: QueueJob[] = []
  let queueNextId = 1
  let queuePolicy = { after: 'wait' as 'wait' | 'auto', delay: 60, remove_done: true }
  let queueRevision = 0
  let queueArmedAt: number | null = null // when a copy completed under the auto policy
  // The start that asked the Canvas for a report (/api/queue/start).
  let queueCheck: { id: number; at: number } | null = null
  // `mapping`: the slots the job is added with ("tool:slot,...", '' for the external feed), unless `tools` says more.
  const queueJob = (
    id: number,
    path: string,
    copies: number,
    done: number,
    { mapping = '0:1', ...extra }: Partial<QueueJob> & { mapping?: string } = {}
  ): QueueJob => ({
    id,
    storage: 'internal',
    path,
    tools: noteFilaments(mappingTools(mapping)),
    side: 'A',
    leveling: 'saved',
    timelapse: false,
    measure: '',
    nozzle: '',
    copies,
    done,
    estimated_seconds: 6000,
    filament_grams: 42,
    added: 1790899200,
    state: done >= copies ? 'done' : 'pending',
    last_result: '',
    present: true,
    ...extra,
  })
  const resetQueue = () => {
    queueJobs = [
      queueJob(1, 'CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode', 5, 2, { estimated_seconds: 1547, filament_grams: 12.5 }),
      queueJob(2, 'CC2_Preview_Gear_housing_PETG.gcode', 2, 0, {
        mapping: '0:0',
        side: 'B',
        leveling: 'calibrate',
        timelapse: true,
        estimated_seconds: 11100,
        filament_grams: 118,
      }),
      queueJob(3, 'CC2_Preview_Phone_stand.gcode', 1, 0, { mapping: '', estimated_seconds: 3300, filament_grams: 23 }),
      queueJob(4, 'CC2_Preview_Vase_missing.gcode', 1, 0, {
        present: false,
        estimated_seconds: null,
        filament_grams: null,
      }),
      queueJob(5, 'CC2_Preview_Calibration_cube.gcode', 1, 1, { estimated_seconds: 1440, filament_grams: 8 }),
    ]
    queueNextId = 6
    queuePolicy = { after: 'wait', delay: 60, remove_done: false }
  }
  resetQueue()
  const queueComplete = (job: QueueJob) => {
    job.done++
    job.last_result = ''
    job.state = job.done >= job.copies ? 'done' : 'pending'
    if (job.state === 'done' && queuePolicy.remove_done) queueJobs = queueJobs.filter(j => j !== job)
    queueArmedAt = queuePolicy.after === 'auto' ? Date.now() : null
    queueRevision++
  }
  // As the backend resolves a start (queue_resolve): every tool on the slot that holds its filament. The slot
  // the job was added with while it holds that filament, otherwise the lowest tray that does; an empty tray
  // (status 0) keeps its label and holds nothing. A tool that knows no filament yet stays on its slot, and an
  // empty slot is then judged by its own label. `use` is null when no slot holds the filament.
  type FeedReason = '' | 'empty' | 'other' | 'absent'
  const queueFilaments = (job: Pick<QueueJob, 'tools'>) =>
    job.tools.map(t => {
      const own = t.slot === null ? null : canvasTrays[t.slot]
      let want: QueueLabel = t
      let use: number | null = null
      let reason: FeedReason = ''
      if (own && own.status !== 0 && (!labelKnown(want) || trayHolds(own.tray_id, want))) use = own.tray_id
      else {
        if (own && !labelKnown(want)) want = trayLabel(own.tray_id)
        const held = labelKnown(want) ? canvasTrays.find(o => o.status !== 0 && trayHolds(o.tray_id, want)) : undefined
        use = held ? held.tray_id : null
        reason = own ? (own.status === 0 ? 'empty' : 'other') : use === null ? 'absent' : ''
      }
      return { tool: t.tool, type: t.type, name: t.name, color: t.color, slot: t.slot, use, reason }
    })
  const queueView = () => {
    // The scene only colours the view: while the printer "prints", the running (or head) job shows as
    // printing. Stored state changes only through the queue routes and /__preview/queue-complete.
    const printing = scene === 'printing' || scene === 'paused'
    const items = queueJobs.map(({ tools, ...job }) => ({
      ...job,
      mapping: mappingText(tools),
      filaments: queueFilaments({ tools }),
    }))
    let active = items.find(j => j.state === 'printing' || j.state === 'starting')
    if (printing) {
      if (!active) active = items.find(j => j.state === 'pending' && j.present)
      if (active) active.state = 'printing'
    }
    const next = items.find(j => j.state === 'pending' && j.present) // as the backend: named even while a copy runs
    return {
      available: true,
      error: '',
      revision: queueRevision,
      printer_idle: scene === 'idle',
      policy: queuePolicy,
      active: active?.id ?? null,
      next: next?.id ?? null,
      auto_in:
        queueArmedAt !== null && !active
          ? Math.max(0, Math.ceil((queueArmedAt + queuePolicy.delay * 1000 - Date.now()) / 1000))
          : null,
      items,
    }
  }
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
  // A measurement is a mesh measured on a plate at a bed temperature; `profile`: the printer keeps it as its own profile.
  type PreviewPlate = { id: string; name: string; side: 'A' | 'B'; z_offset: number; measure: string }
  type PreviewMeasure = {
    id: string
    plate: string
    temp: number
    nozzle: string
    measured: number
    tilt: number
    profile: boolean
  }
  type PreviewNozzle = { id: string; name: string; diameter: number; z_offset: number }
  let plates: PreviewPlate[] = []
  let measures: PreviewMeasure[] = []
  let nozzles: PreviewNozzle[] = []
  let plateNozzle = ''
  let plateSlots: Record<'A' | 'B', number> = { A: 1, B: 2 }
  let plateCurrent = ''
  let plateMatch = false // #296: match the mesh to prints started elsewhere
  let plateBlendsLive = false // the printer has loaded the blended meshes
  let plateResult = ''
  // The calibration CC2 Control runs; in the preview each POST /__preview/calibration-step moves it one stage on.
  const idleCalibration = () => ({
    stage: 'off',
    side: 'A' as 'A' | 'B',
    temp: 0,
    soak: 0,
    remaining: 0,
    bed: 25 as number | null,
    plate: '',
    nozzle: '',
    measure: '',
    result: '',
    error: '',
    detail: '',
  })
  let calibration = idleCalibration()
  let nextTilt = 6
  const newId = () => Array.from({ length: 16 }, () => Math.floor(Math.random() * 16).toString(16)).join('')
  const resetPlates = () => {
    plates = [
      { id: 'a1b2c3d4e5f60718', name: 'Smooth PEI', side: 'A', z_offset: -0.02, measure: 'aa00000000000060' },
      { id: '0f1e2d3c4b5a6978', name: 'Textured PEI', side: 'B', z_offset: 0.01, measure: 'bb00000000000060' },
      { id: '1234567890abcdef', name: 'Cool Plate', side: 'A', z_offset: 0, measure: 'cc00000000000060' },
    ]
    const brass = 'dd00000000000004'
    measures = [
      {
        id: 'aa00000000000060',
        plate: 'a1b2c3d4e5f60718',
        temp: 60,
        nozzle: brass,
        measured: 1790800000,
        tilt: 1,
        profile: true,
      },
      {
        id: 'aa00000000000080',
        plate: 'a1b2c3d4e5f60718',
        temp: 80,
        nozzle: brass,
        measured: 1790810000,
        tilt: 4,
        profile: true,
      },
      {
        id: 'aa00000000000100',
        plate: 'a1b2c3d4e5f60718',
        temp: 100,
        nozzle: '',
        measured: 1790820000,
        tilt: 5,
        profile: false,
      },
      {
        id: 'bb00000000000060',
        plate: '0f1e2d3c4b5a6978',
        temp: 60,
        nozzle: '',
        measured: 1790700000,
        tilt: 2,
        profile: true,
      },
      {
        id: 'cc00000000000060',
        plate: '1234567890abcdef',
        temp: 60,
        nozzle: '',
        measured: 1790600000,
        tilt: 3,
        profile: false,
      },
    ]
    nozzles = [
      { id: brass, name: '0.4 brass', diameter: 0.4, z_offset: 0 },
      { id: 'dd00000000000006', name: '0.6 hardened', diameter: 0.6, z_offset: 0.02 },
    ]
    plateNozzle = brass
    plateSlots = { A: 1, B: 2 }
    plateCurrent = 'a1b2c3d4e5f60718'
    plateResult = ''
    calibration = idleCalibration()
  }
  resetPlates()
  const plateMeasures = (p: PreviewPlate) =>
    measures.filter(m => m.plate === p.id).sort((a, b) => a.temp - b.temp || a.measured - b.measured)
  const plateBase = (p: PreviewPlate) => measures.find(m => m.id === p.measure) || plateMeasures(p)[0]
  const plateLibrary = () => {
    const current = plates.find(p => p.id === plateCurrent)
    const installed = nozzles.find(n => n.id === plateNozzle)
    return {
      available: true,
      error: '',
      current: plateCurrent,
      pending: '',
      result: plateResult,
      z_applied: Boolean(current),
      z_effective: current ? Math.round((current.z_offset + (installed?.z_offset || 0)) * 1000) / 1000 : null,
      nozzle: plateNozzle,
      mesh_profile: 'default',
      print_mesh: { state: 'off', measure: '', result: '', source: '' },
      match: {
        enabled: plateMatch,
        result: '',
        blend_temp: 0,
        blends: plateMatch
          ? {
              state: plateBlendsLive ? 'ready' : 'written',
              temps: [70, 75, 85, 95].map(temp => ({ temp, live: plateBlendsLive })),
            }
          : { state: 'off', temps: [] },
      },
      calibration,
      slots: { A: plateSlots.A ? 'mesh' : 'empty', B: plateSlots.B ? 'mesh' : 'empty' },
      nozzles,
      plates: plates.map(p => ({
        ...p,
        in_printer: plateSlots[p.side] === plateBase(p)?.tilt,
        measures: plateMeasures(p).map(({ plate, tilt, ...m }) => ({
          ...m,
          slot: plateSlots[p.side] === tilt,
          mesh: plateMesh(tilt),
        })),
      })),
    }
  }
  // Spool library as /api/spools reports it; tracking starts off so other previews never see its questions.
  // Slot 3 reports a purple no spool has, so its question suggests nothing.
  const trayColors = ['#EF5350', '#42A5F5', '#8E24AA', '#66BB6A']
  type PreviewSpool = Record<string, any> & { id: string }
  let spoolsOn = false
  let spoolmanOn = false,
    spoolmanUrl = '',
    spoolmanHasAuth = false
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
    spoolman_id: '',
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
      // The blue of slot 2 from another maker: the tray names ELEGOO, so its question leaves this one out.
      spool('aa00000000000009', 'Polymaker PLA Blue', 'PLA', '#42A5F5', 700, { brand: 'Polymaker' }),
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
      spoolman_enabled: spoolmanOn,
      spoolman_url: spoolmanUrl,
      spoolman_has_auth: spoolmanHasAuth,
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
  const unloadState = () => ({
    ok: true,
    editable: true,
    min: 60,
    max: unload.extended ? 1000 : 350,
    extended: unload.extended,
    configured: unload.configured,
    override: unload.override,
    pending_restart: unload.pending,
    restart: 'none',
  })
  const reset = () => {
    speed = 100
    flow = 100
    autoRefill = false
    unload = { configured: 85, override: null, pending: false, extended: false }
    videos = {}
    deleted = new Set()
    cameraViewer = null
    tramSession = false
    tramHeights = { '30,30': 1.8, '230,30': 1.85, '230,225': 1.82, '30,225': 1.79 }
    screwSessions = seedScrewHistory()
    consoleJob = {
      busy: false,
      completed: false,
      success: false,
      generation: 0,
      command: '',
      output: '// CC2 UI preview: no hardware commands are executed.\n// All data is simulated.\n',
    }
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
      screws_session: {
        active: tramSession && scene === 'idle',
        xy_homed: tramSession && scene === 'idle',
      },
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
        const reqUrl = new URL(req.url || '/', 'http://localhost')
        const path = reqUrl.pathname
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
              return reply({ ...consoleJob })
            case '/api/screws/history':
              return reply({ sessions: screwSessions })
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
              return reply(demoExcludeObjects())
            case '/api/gcode-files/layer-path': {
              const file = reqUrl.searchParams.get('file') || ''
              const layer = Number(reqUrl.searchParams.get('layer'))
              const storage = reqUrl.searchParams.get('storage') || 'internal'
              if (!file || !Number.isInteger(layer) || layer < 1)
                return reply({ error: 'invalid layer path request' }, 400)
              if (layer > 227) return reply({ error: 'layer not found' }, 404)
              return reply(demoLayerPath(file, layer, storage))
            }
            case '/api/queue':
              return reply(queueView())
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
            case '/api/canvas/unload':
              return reply(unloadState())
            case '/api/calibration-tools/status':
              return reply({
                busy: false,
                generation: 0,
                error: 'none',
                mode: 0,
                active: null,
                original: null,
                configured: 28.8,
                saved: null,
                editable: true,
                pending_restart: false,
                pending_conflict: false,
                reports: [],
                cell: { stage: 0, owned: false, max_grams: 0, counts_per_gram: 0, tare_counts: null, grams: 0 },
              })
            case '/api/calibration-tools/action':
              return reply({ error: 'Calibration tools are read-only in this preview' }, 409)
            case '/api/nozzle-expansion': {
              const factory = [
                -0.039173, -0.033207, -0.027035, -0.017856, -0.009456, -0.002735, 0.006161, 0.01315, 0.021694, 0.028802,
                0.034645, 0.040412, 0.048493, 0.05439, 0.063596, 0.07289,
              ]
              const saved = Array.from({ length: 20 }, (_, i) => +(0.00081 * (130 + 10 * i - 140) - 0.0094).toFixed(6))
              return reply({
                ok: true,
                enabled: true,
                available: true,
                editable: true,
                state: 'idle',
                result: 'saved',
                detail: '',
                start: 130,
                end: 320,
                step: 10,
                delay: 60,
                samples: 5,
                elapsed: 0,
                nozzle: 26,
                target: 0,
                reference: 140,
                factory: { start: 100, diff: 10, values: factory },
                override: { start: 130, diff: 10, values: saved },
                measured: { start: 130, diff: 10, values: saved },
                slope_um: 0.81,
                residual_um: 1.9,
                pending_restart: true,
                restart: 'none',
              })
            }
            case '/api/canvas':
              return reply({
                auto_refill: autoRefill,
                result: {
                  canvas_info: {
                    canvas_list: [
                      {
                        connected: 1,
                        tray_list: canvasTrays,
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
          '/api/screws/history/clear',
          '/api/preferences',
          '/api/canvas/auto-refill',
          '/api/canvas/unload',
          '/api/canvas/unload/restart',
          '/api/history/refresh',
          '/api/history/delete',
          '/api/camera/claim',
          '/api/history/timelapse',
          '/api/plates/save',
          '/api/plates/create',
          '/api/plates/edit',
          '/api/plates/delete',
          '/api/plates/mount',
          '/api/plates/unmount',
          '/api/plates/match',
          '/api/plates/blends/restart',
          '/api/plates/measure',
          '/api/plates/measure/delete',
          '/api/plates/keep',
          '/api/plates/nozzle',
          '/api/plates/nozzle/delete',
          '/api/plates/nozzle/select',
          '/api/plates/measure/edit',
          '/api/plates/calibrate',
          '/api/plates/calibrate/cancel',
          '/__preview/calibration-step',
          '/api/spoolman',
          '/api/spools/enable',
          '/api/spools/save',
          '/api/spools/delete',
          '/api/spools/assign',
          '/api/spools/dismiss',
          '/api/spools/adjust',
          '/__preview/spool-insert',
          '/api/queue/add',
          '/api/queue/start',
          '/api/queue/remove',
          '/api/queue/reorder',
          '/api/queue/copies',
          '/api/queue/filament',
          '/api/queue/policy',
          '/api/queue/clear-done',
          '/__preview/queue-complete',
          '/__preview/canvas-tray',
          '/api/queue/hold',
          '/api/queue/abandon',
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
            if (path === '/__preview/queue-complete' && req.method === 'POST') {
              const active = queueJobs.find(j => j.state === 'starting' || j.state === 'printing')
              if (!active) return reply({ ok: false, error: 'No running copy' }, 409)
              queueComplete(active)
              return reply({ ok: true })
            }
            // "tray\nstatus[\ncolour[\nname]]": a spool runs out (0) or is put back (1), with the colour and the
            // filament name of its label; "-" leaves the label without that colour or name.
            if (path === '/__preview/canvas-tray' && req.method === 'POST') {
              const [id, status, colour, name] = body.replace(/\n+$/, '').split('\n')
              const tray = canvasTrays[Number(id)]
              if (!tray || !['0', '1', '2'].includes(status)) return reply({ ok: false, error: 'Invalid tray' }, 400)
              tray.status = Number(status)
              if (colour) tray.color = tray.filament_color = colour === '-' ? '' : colour
              if (name) tray.filament_name = tray.material = name === '-' ? '' : name
              queueRevision++
              return reply({ ok: true })
            }
            if (path.startsWith('/api/queue/') && req.method === 'POST') {
              const lines = body.replace(/\n+$/, '').split('\n')
              const find = () => queueJobs.find(j => j.id === Number(lines[0]))
              queueRevision++
              if (path === '/api/queue/add') {
                if (lines.length < 9) return reply({ ok: false, error: 'Invalid queue request' }, 400)
                const [
                  storage,
                  file,
                  mapping,
                  side,
                  leveling,
                  timelapse,
                  measure,
                  nozzle,
                  copies,
                  position,
                  seconds,
                  grams,
                ] = lines
                // The filaments of tools added without a slot follow the estimates.
                const free = filamentTools(lines.slice(12))
                if (!free) return reply({ ok: false, error: 'Invalid filament' }, 400)
                const tools = [...mappingTools(mapping || ''), ...free]
                if (new Set(tools.map(t => t.tool)).size !== tools.length)
                  return reply({ ok: false, error: 'A tool is named twice' }, 400)
                const job = queueJob(queueNextId++, file, Number(copies) || 1, 0, {
                  storage: storage === 'usb' ? 'usb' : 'internal',
                  tools: noteFilaments(tools),
                  side: side === 'B' ? 'B' : 'A',
                  leveling: leveling || 'saved',
                  timelapse: timelapse === '1',
                  measure: measure || '',
                  nozzle: nozzle || '',
                  estimated_seconds: seconds ? Number(seconds) : null,
                  filament_grams: grams ? Number(grams) : null,
                  added: Math.floor(Date.now() / 1000),
                })
                const head = queueJobs.findIndex(j => j.state === 'pending')
                if (position === 'next' && head >= 0) queueJobs.splice(head, 0, job)
                else queueJobs.push(job)
                return reply({ ok: true, id: job.id, count: queueJobs.length }, 201)
              }
              if (path === '/api/queue/start') {
                if (scene !== 'idle')
                  return reply({ accepted: false, error: 'Starting a file requires an idle printer' }, 409)
                const job = lines[0] ? find() : queueJobs.find(j => j.state === 'pending' && j.present)
                if (!job) return reply({ ok: false, error: 'No such queued job' }, 404)
                if (job.state !== 'pending' || !job.present)
                  return reply({ ok: false, error: 'This job has no copy left to print' }, 409)
                // As on the printer: the copy runs until it is reported finished (/__preview/queue-complete,
                // or the scene leaving printing); nothing is counted at the start.
                if (queueJobs.some(j => j.state === 'starting' || j.state === 'printing'))
                  return reply({ ok: false, error: 'A queued copy is already running' }, 409)
                // As on the backend, a start fed from the Canvas first asks it for a report and goes on only
                // once it has come (here 300 ms later): until then it is refused with `checking`.
                if (queueFilaments(job).length) {
                  const now = Date.now()
                  if (!queueCheck || queueCheck.id !== job.id || now - queueCheck.at > 10000)
                    queueCheck = { id: job.id, at: now }
                  if (now - queueCheck.at < 300)
                    return reply({ ok: false, error: 'Reading the Canvas slots before the start', checking: true }, 409)
                  queueCheck = null
                }
                // A filament of the job that is in no slot refuses the start, as on the backend.
                const missing = queueFilaments(job).find(f => f.use === null)
                if (missing)
                  return reply(
                    {
                      ok: false,
                      error:
                        missing.reason === 'empty' && missing.slot !== null
                          ? `Canvas slot ${missing.slot + 1} is empty and no other slot holds the same filament`
                          : `No Canvas slot holds the job's filament: ${missing.name || missing.type} ${missing.color}`,
                    },
                    409
                  )
                job.state = 'starting'
                job.last_result = ''
                queueArmedAt = null
                return reply({ accepted: true, method: 1020, storage_media: 'local', filename: job.path }, 202)
              }
              const job = find()
              if (path === '/api/queue/hold') {
                queueArmedAt = null
                return reply({ ok: true })
              }
              if (path === '/api/queue/abandon') {
                const waiting = queueJobs.find(j => j.state === 'starting')
                if (!waiting) return reply({ ok: false, error: 'No start is waiting' }, 409)
                waiting.state = 'pending'
                waiting.last_result = 'not_started'
                return reply({ ok: true })
              }
              if (path === '/api/queue/clear-done') {
                queueJobs = queueJobs.filter(j => j.state !== 'done')
                return reply({ ok: true })
              }
              if (path === '/api/queue/policy') {
                const [after, delay, remove] = lines
                if ((after !== 'wait' && after !== 'auto') || !(Number(delay) >= 10 && Number(delay) <= 3600))
                  return reply({ ok: false, error: 'Invalid queue policy' }, 400)
                queuePolicy = { after, delay: Number(delay), remove_done: remove === '1' }
                queueArmedAt = null
                return reply({ ok: true })
              }
              if (!job) return reply({ ok: false, error: 'No such queued job' }, 404)
              if (path === '/api/queue/remove') {
                queueJobs = queueJobs.filter(j => j !== job)
                return reply({ ok: true })
              }
              if (path === '/api/queue/reorder') {
                const to = Math.min(Math.max(0, Number(lines[1]) || 0), queueJobs.length - 1)
                queueJobs.splice(queueJobs.indexOf(job), 1)
                queueJobs.splice(to, 0, job)
                return reply({ ok: true })
              }
              // "id\nmapping\nfilament lines": other filaments for tools of the job, by slot or by themselves.
              if (path === '/api/queue/filament') {
                const free = filamentTools(lines.slice(2))
                if (!free) return reply({ ok: false, error: 'Invalid filament' }, 400)
                const named = [...mappingTools(lines[1] || ''), ...free]
                if (new Set(named.map(t => t.tool)).size !== named.length)
                  return reply({ ok: false, error: 'A tool is named twice' }, 400)
                if (!named.length) return reply({ ok: false, error: 'No filament to change' }, 400)
                if (named.some(t => !job.tools.some(own => own.tool === t.tool)))
                  return reply({ ok: false, error: 'This job has no such tool' }, 409)
                job.tools = noteFilaments(job.tools.map(own => named.find(t => t.tool === own.tool) ?? own))
                return reply({ ok: true })
              }
              if (path === '/api/queue/copies') {
                const n = Number(lines[1])
                if (!(n >= 1 && n <= 99) || n < job.done)
                  return reply({ ok: false, error: 'Fewer copies than already printed' }, 409)
                job.copies = n
                job.state = job.done >= n ? 'done' : job.state === 'done' ? 'pending' : job.state
                return reply({ ok: true })
              }
              return reply({ error: 'Unknown queue operation' }, 404)
            }
            if (path === '/api/spoolman' && req.method === 'POST') {
              const form = Object.fromEntries(
                body
                  .split('\n')
                  .filter(Boolean)
                  .map(line => {
                    const i = line.indexOf('=')
                    return [line.slice(0, i), line.slice(i + 1)]
                  })
              )
              if (form.enabled !== undefined) spoolmanOn = form.enabled === '1'
              if (form.url !== undefined) spoolmanUrl = form.url
              if (form.basic_auth !== undefined) spoolmanHasAuth = Boolean(form.basic_auth)
              spoolRevision++
              return reply({ ok: true, simulated: true })
            }
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
            if (path === '/api/canvas/unload' && req.method === 'POST') {
              const text = body.trim()
              const max = unload.extended ? 1000 : 350
              const value = text === 'default' ? null : /^\d{2,4}$/.test(text) ? Number(text) : Number.NaN
              if (value !== null && !(value >= 60 && value <= max))
                return reply({ ok: false, error: `Enter a whole number of millimetres from 60 to ${max}` }, 400)
              if (scene !== 'idle') return reply({ ok: false, error: 'The printer must be idle' }, 409)
              if (value !== unload.override) unload = { ...unload, override: value, pending: true }
              return reply(unloadState())
            }
            if (path === '/api/canvas/unload/restart' && req.method === 'POST') {
              if (body.trim() !== 'RESTART')
                return reply({ ok: false, error: 'Explicit restart confirmation required' }, 400)
              if (!unload.pending)
                return reply({ ok: false, error: 'No saved pull-back distance is waiting for a restart' }, 409)
              unload = { ...unload, pending: false } // the preview "restarts" at once
              return reply({ ok: true, restarting: true, simulated: true }, 202)
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
            if (path === '/__preview/calibration-step' && req.method === 'POST') {
              const c = calibration
              if (body.trim() === 'fail' && c.stage !== 'off')
                calibration = { ...c, stage: 'off', result: 'failed', error: c.stage }
              else if (c.stage === 'homing') calibration = { ...c, stage: 'heating', bed: 25 }
              else if (c.stage === 'heating') calibration = { ...c, stage: 'soaking', bed: c.temp, remaining: c.soak }
              else if (c.stage === 'soaking') calibration = { ...c, stage: 'probing', remaining: 0 }
              else if (c.stage === 'probing') {
                plateSlots[c.side] = nextTilt++ // the printer saved a new side mesh
                calibration = c.plate ? { ...c, stage: 'saving' } : { ...c, stage: 'off', result: 'done', bed: 25 }
              } else if (c.stage === 'saving') {
                const plate = plates.find(p => p.id === c.plate)
                if (!plate) return reply({ error: 'Unknown plate' }, 404)
                let m = measures.find(x => x.plate === plate.id && x.temp === c.temp && x.nozzle === c.nozzle)
                if (!m) {
                  m = {
                    id: newId(),
                    plate: plate.id,
                    temp: c.temp,
                    nozzle: c.nozzle,
                    measured: 0,
                    tilt: 0,
                    profile: true,
                  }
                  measures.push(m)
                }
                Object.assign(m, { tilt: plateSlots[plate.side], measured: 1790990000, profile: true })
                plate.measure = m.id
                plateCurrent = plate.id
                calibration = { ...c, stage: 'off', result: 'saved', measure: m.id, bed: 25 }
              }
              return reply(calibration)
            }
            if (path.startsWith('/api/plates/') && req.method === 'POST') {
              const lines = body.replace(/\n+$/, '').split('\n')
              const plate = plates.find(p => p.id === lines[0])
              const idleOnly = [
                '/api/plates/save',
                '/api/plates/create',
                '/api/plates/mount',
                '/api/plates/measure',
                '/api/plates/keep',
                '/api/plates/calibrate',
              ]
              if (idleOnly.includes(path) && scene !== 'idle')
                return reply({ ok: false, error: 'The printer must be idle' }, 409)
              // Before a side mesh is replaced, the measurement it holds keeps a printer profile.
              const keep = (side: 'A' | 'B') => {
                const held = measures.find(
                  m => m.tilt === plateSlots[side] && plates.find(p => p.id === m.plate)?.side === side
                )
                if (held) held.profile = true
                return held
              }
              if (path === '/api/plates/blends/restart') {
                if (!plateMatch || plateBlendsLive)
                  return reply({ ok: false, error: 'No blended meshes are waiting for a restart' }, 409)
                plateBlendsLive = true
                return reply({ accepted: true, reboot: true }, 202)
              }
              if (path === '/api/plates/match') {
                if (body !== 'on' && body !== 'off') return reply({ ok: false, error: 'Use on or off' }, 400)
                plateMatch = body === 'on'
                return reply({ ok: true, enabled: plateMatch })
              }
              if (path === '/api/plates/unmount') {
                plateCurrent = ''
                return reply({ mounted: false })
              }
              if (path === '/api/plates/calibrate') {
                if (calibration.stage !== 'off')
                  return reply({ ok: false, error: 'A bed mesh calibration is already running' }, 409)
                const [side, temp, soak, nozzle = '', plateId = ''] = lines as [
                  'A' | 'B',
                  string,
                  string,
                  string?,
                  string?,
                ]
                keep(side)
                calibration = {
                  ...idleCalibration(),
                  stage: 'homing',
                  side,
                  temp: Number(temp),
                  soak: Number(soak) * 60,
                  nozzle,
                  plate: plateId,
                }
                return reply({ started: true }, 202)
              }
              if (path === '/api/plates/calibrate/cancel') {
                if (['probing', 'saving'].includes(calibration.stage))
                  return reply(
                    { ok: false, error: 'The probing has started; use the emergency stop to interrupt it' },
                    409
                  )
                if (calibration.stage !== 'off') calibration = { ...calibration, stage: 'off', result: 'cancelled' }
                return reply({ cancelled: true })
              }
              if (path === '/api/plates/measure/edit') {
                const [id, temp, nozzle = ''] = lines
                const m = measures.find(x => x.id === id)
                if (!m) return reply({ ok: false, error: 'Unknown measurement' }, 404)
                if (
                  measures.some(x => x !== m && x.plate === m.plate && x.temp === Number(temp) && x.nozzle === nozzle)
                )
                  return reply(
                    { ok: false, error: 'The plate already has a measurement at this temperature with this nozzle' },
                    409
                  )
                Object.assign(m, { temp: Number(temp), nozzle })
                return reply({ saved: true })
              }
              if (path === '/api/plates/keep') {
                const held = keep(lines[0] as 'A' | 'B')
                return reply({ measure: held?.id || '', profile: Boolean(held) })
              }
              if (path === '/api/plates/nozzle') {
                const [id, name, diameter, z] = lines
                if (nozzles.some(n => n.name === name && n.id !== id))
                  return reply({ ok: false, error: 'A nozzle with this name already exists' }, 409)
                const found = nozzles.find(n => n.id === id)
                if (id && !found) return reply({ ok: false, error: 'Unknown nozzle' }, 404)
                const fields = { name, diameter: Number(diameter), z_offset: Number(z) }
                if (found) Object.assign(found, fields)
                else nozzles.push({ id: newId(), ...fields })
                return reply({
                  saved: true,
                  id: found?.id || nozzles[nozzles.length - 1].id,
                  applied: scene === 'idle',
                })
              }
              if (path === '/api/plates/nozzle/delete') {
                nozzles = nozzles.filter(n => n.id !== lines[0])
                for (const m of measures) if (m.nozzle === lines[0]) m.nozzle = ''
                if (plateNozzle === lines[0]) plateNozzle = ''
                return reply({ deleted: true })
              }
              if (path === '/api/plates/nozzle/select') {
                plateNozzle = lines[0] || ''
                return reply({ selected: true, applied: scene === 'idle' })
              }
              if (path === '/api/plates/create') {
                const [side, name, z] = lines as ['A' | 'B', string, string]
                if (plates.some(p => p.name === name))
                  return reply({ error: 'A plate with this name already exists' }, 409)
                const id = newId()
                plates.push({ id, name, side, z_offset: Number(z), measure: '' })
                return reply({ saved: true, id, measure: '' }, 201)
              }
              if (path === '/api/plates/save') {
                const [side, name, z, temp, nozzle] = lines as ['A' | 'B', string, string, string, string?]
                if (plates.some(p => p.name === name))
                  return reply({ ok: false, error: 'A plate with this name already exists' }, 409)
                const id = newId(),
                  measure = newId()
                plates.push({ id, name, side, z_offset: Number(z), measure })
                measures.push({
                  id: measure,
                  plate: id,
                  temp: Number(temp || 60),
                  nozzle: nozzle || '',
                  measured: 1790900000,
                  tilt: plateSlots[side],
                  profile: true,
                })
                return reply({ saved: true, id, measure, profile: true }, 201)
              }
              if (path === '/api/plates/measure/delete') {
                const gone = measures.find(m => m.id === lines[0])
                if (!gone) return reply({ ok: false, error: 'Unknown measurement' }, 404)
                const owner = plates.find(p => p.id === gone.plate)
                if (!owner || measures.filter(m => m.plate === owner.id).length < 2)
                  return reply(
                    { ok: false, error: 'A plate keeps at least one measurement; delete the plate instead' },
                    409
                  )
                measures = measures.filter(m => m !== gone)
                if (owner.measure === gone.id)
                  owner.measure = plateMeasures(owner).reduce((a, b) => (b.measured > a.measured ? b : a)).id
                return reply({ deleted: true })
              }
              if (!plate) return reply({ ok: false, error: 'Unknown plate' }, 404)
              if (path === '/api/plates/edit') {
                plate.name = lines[1]
                plate.z_offset = Number(lines[2])
                return reply({ saved: true, applied: plateCurrent === plate.id && scene === 'idle' })
              }
              if (path === '/api/plates/delete') {
                plates = plates.filter(p => p !== plate)
                measures = measures.filter(m => m.plate !== plate.id)
                if (plateCurrent === plate.id) plateCurrent = ''
                return reply({ deleted: true })
              }
              if (path === '/api/plates/measure') {
                // The side mesh just calibrated becomes the plate's measurement at that temperature.
                const [, temp, nozzle = ''] = lines
                let m = measures.find(x => x.plate === plate.id && x.temp === Number(temp) && x.nozzle === nozzle)
                if (!m) {
                  m = { id: newId(), plate: plate.id, temp: Number(temp), nozzle, measured: 0, tilt: 0, profile: true }
                  measures.push(m)
                }
                Object.assign(m, { tilt: plateSlots[plate.side], measured: 1790950000, profile: true })
                plate.measure = m.id
                plateCurrent = plate.id
                plateResult = 'mounted'
                return reply({ saved: true, measure: m.id, profile: true, applied: true })
              }
              const reboot = lines.includes('REBOOT')
              const chosen = lines.slice(1).find(x => x !== 'REBOOT')
              const base = chosen ? measures.find(m => m.id === chosen && m.plate === plate.id) : plateBase(plate)
              if (!base) return reply({ ok: false, error: 'Unknown measurement' }, 404)
              if (plateSlots[plate.side] !== base.tilt && !reboot)
                return reply(
                  {
                    ok: false,
                    reboot_required: true,
                    error: 'Writing this mesh to the printer needs a printer restart',
                  },
                  409
                )
              const restart = plateSlots[plate.side] !== base.tilt
              if (restart) keep(plate.side)
              plateSlots[plate.side] = base.tilt // the simulated restart is instant
              base.profile = true
              plate.measure = base.id
              plateCurrent = plate.id
              plateResult = 'mounted'
              return reply({ mounted: true, reboot: restart }, restart ? 202 : 200)
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
            if (path === '/api/screws/history/clear' && req.method === 'POST') {
              screwSessions = []
              return reply({ ok: true })
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
              else if (action.startsWith('screws:tram:') && scene === 'idle') {
                if (action === 'screws:tram:start') {
                  tramSession = true
                  closeOpenScrew()
                  screwSessions.unshift({
                    kind: 'session',
                    started: screwNow(),
                    ended: null,
                    measurements: [{ time: screwNow(), kind: 'start', corner: null, twice: false, z: heightsZ() }],
                  })
                  trimScrewHistory()
                  finishTram('SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\nG28\n', ['30,30', '230,30', '230,225', '30,225'])
                  return reply({ accepted: true, simulated: true })
                }
                if (!tramSession) return reply({ error: 'Screw tramming session is not active' }, 409)
                const probe = action.match(/^screws:tram:probe:(FL|FR|RR|RL)(:2)?$/)
                if (probe) {
                  const corner = tramCorner(probe[1])
                  if (!corner) return reply({ error: 'Unknown screw corner' }, 400)
                  const first = tramHeights[corner.key] - 0.02
                  const second = probe[2] ? first - 0.001 : first
                  const open = screwSessions.find(s => s.ended == null)
                  open?.measurements.push({
                    time: screwNow(),
                    kind: 'probe',
                    corner: probe[1],
                    twice: Boolean(probe[2]),
                    z: { [probe[1]]: +first.toFixed(4) },
                    ...(probe[2] ? { z2: { [probe[1]]: +second.toFixed(4) } } : {}),
                  })
                  tramHeights[corner.key] = second
                  consoleJob = {
                    busy: false,
                    completed: true,
                    success: true,
                    generation: consoleJob.generation + 1,
                    command: `SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\nG28 Z\nG1 X${corner.x} Y${corner.y}\n`,
                    output: `SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\n${probeLines(corner.key, first)}${probe[2] ? probeLines(corner.key, second) : ''}`,
                  }
                  return reply({ accepted: true, simulated: true })
                }
                if (action === 'screws:tram:check' || action === 'screws:tram:check:2') {
                  const twice = action.endsWith(':2')
                  const z = heightsZ()
                  screwSessions
                    .find(s => s.ended == null)
                    ?.measurements.push({
                      time: screwNow(),
                      kind: 'check',
                      corner: null,
                      twice,
                      z,
                      ...(twice ? { z2: z } : {}),
                    })
                  finishTram('SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\nG28 Z\n', ['30,30', '230,30', '230,225', '30,225'])
                  return reply({ accepted: true, simulated: true })
                }
                if (action === 'screws:tram:end') {
                  tramSession = false
                  closeOpenScrew()
                  consoleJob = {
                    ...consoleJob,
                    generation: consoleJob.generation + 1,
                    command: 'M104 S0\nM140 S0',
                    completed: true,
                    success: true,
                    busy: false,
                  }
                  return reply({ accepted: true, simulated: true })
                }
                return reply({ error: 'Unknown screw tramming action' }, 400)
              } else return reply({ error: 'Operation disabled in isolated preview' }, 403)
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
