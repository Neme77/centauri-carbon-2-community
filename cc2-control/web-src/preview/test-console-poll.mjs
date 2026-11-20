import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

// GET /api/console?since=<generation>:<length>:<instance> (CC2 Control answers "unchanged":true without the output while
// nothing new is there) and the slower look of the screw view while nothing runs.
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({
  root,
  configFile: path.join(root, 'vite.config.ts'),
  mode: 'demo',
  server: { port: 0, host: '127.0.0.1' },
})
let browser
const errors = []
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms))
const wait = async (check, what, ms = 8000) => {
  const end = Date.now() + ms
  while (Date.now() < end) {
    if (await check()) return
    await sleep(50)
  }
  assert.fail(`timed out: ${what}`)
}
const measureOutputOf = heights =>
  Object.entries(heights)
    .map(([key, z]) => {
      const [x, y] = key.split(',')
      return `${`// probe at ${x}.000,${y}.000 is z=${z.toFixed(6)}\n`.repeat(3)}// Result is z=${z.toFixed(6)}\n`
    })
    .join('')
const measureOutput = measureOutputOf({ '30,30': 1.8, '230,30': 1.74, '230,225': 1.72, '30,225': 1.7 })
// The same text length with other numbers: the console of a CC2 Control that started again and measured differently.
const measureOutputRestarted = measureOutputOf({ '30,30': 1.7, '230,30': 1.74, '230,225': 1.72, '30,225': 1.8 })

// A stand-in for CC2 Control's console: the since rule of console_json_query ('new'), one that knows the parameter but
// not the instance ('noinstance': the first build of it) or an older one that ignores it ('old').
const INSTANCE_A = '00000000000000a1'
const INSTANCE_B = '00000000000000b2'
const makeConsole = (mode = 'new') => {
  const model = {
    mode,
    instance: INSTANCE_A,
    fail: false,
    busy: false,
    completed: true,
    success: true,
    generation: 5,
    command: 'M105',
    output: 'ready\n',
    log: [],
  }
  model.handle = route => {
    if (route.request().method() !== 'GET') return route.fallback()
    if (model.fail) {
      model.log.push({ since: null, failed: true })
      return route.abort()
    }
    const since = new URL(route.request().url()).searchParams.get('since')
    const match =
      model.mode === 'noinstance' ? /^(\d+):(\d+)$/.exec(since || '') : /^(\d+):(\d+)(?::([0-9a-f]{16}))?$/.exec(since || '')
    const length = Buffer.byteLength(model.output)
    const head = {
      busy: model.busy,
      completed: model.completed,
      success: model.success,
      generation: model.generation,
      command: model.command,
    }
    const named = model.mode === 'new' && match ? { output_len: length, instance: model.instance } : {}
    const same =
      model.mode !== 'old' &&
      match &&
      !model.busy &&
      Number(match[1]) === model.generation &&
      Number(match[2]) === length &&
      (model.mode === 'noinstance' || match[3] === model.instance)
    const body = same
      ? { ...head, unchanged: true, output_len: length, ...(model.mode === 'new' ? { instance: model.instance } : {}) }
      : {
          ...head,
          output: model.output,
          ...(model.mode === 'new' ? named : model.mode === 'noinstance' && match ? { output_len: length } : {}),
        }
    model.log.push({ since, unchanged: Boolean(same), busy: model.busy })
    return route.fulfill({ json: body })
  }
  return model
}

const consolePattern = /\/api\/console(\?.*)?$/
try {
  await server.listen()
  browser = await chromium.launch({
    headless: true,
    ...(process.env.CC2_BROWSER_PATH
      ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox'] }
      : process.env.CC2_BROWSER_EXECUTABLE
        ? { executablePath: process.env.CC2_BROWSER_EXECUTABLE }
        : {}),
  })
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  const idle = await fetch(`${origin}/__preview/scenario`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ scene: 'idle' }),
  })
  assert.ok(idle.ok, 'the idle scene is set')
  const open = async () => {
    const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
    page.on('pageerror', e => errors.push(e.message))
    return page
  }

  // 1 — the console page: nothing new costs no output, the text stays, new output and a new generation arrive.
  {
    const model = makeConsole()
    const page = await open()
    await page.route(consolePattern, model.handle)
    await page.goto(`${origin}#console`)
    const log = page.locator('pre').filter({ hasText: 'ready' })
    await log.first().waitFor({ timeout: 10000 })
    assert.equal(model.log[0].since, '0:0', 'the first request asks for nothing it cannot know and names no instance')
    assert.equal(model.log[0].unchanged, false, 'the first answer is in full')
    await wait(() => model.log.filter(l => l.unchanged).length >= 2, 'two unchanged answers', 12000)
    assert.ok(await log.first().isVisible(), 'an unchanged answer does not blank the text')
    assert.match(await log.first().innerText(), /ready/)
    const quiet = model.log.filter(l => l.unchanged)
    assert.ok(quiet.every(l => l.since === `5:6:${INSTANCE_A}`), `later requests name the text they hold and its instance: ${JSON.stringify(quiet)}`)

    // A running command always answers in full and the page shows the output as it grows.
    model.busy = true
    model.completed = false
    model.generation = 6
    model.command = 'G28'
    for (const n of [1, 2, 3]) {
      model.output = `homing ${'.'.repeat(n)}\n`
      await page.locator('pre').filter({ hasText: `homing ${'.'.repeat(n)}` }).first().waitFor({ timeout: 4000 })
    }
    assert.equal(model.log.filter(l => l.busy && l.unchanged).length, 0, 'a running command never gets "unchanged"')

    // The command ends without more output: the state follows, the text stays.
    const before = model.log.length
    model.busy = false
    model.completed = true
    await wait(() => model.log.length > before + 1, 'polls after the command', 8000)
    assert.ok(model.log.slice(before).some(l => l.unchanged), 'the finished, unchanged console is answered short')
    assert.match(await page.locator('pre').filter({ hasText: 'homing ...' }).first().innerText(), /homing \.\.\./)

    // Another command with output of the same length: the generation differs, so the text comes in full.
    model.generation = 7
    model.command = 'M114'
    model.output = 'homing ,,,\n'
    await page.locator('pre').filter({ hasText: 'homing ,,,' }).first().waitFor({ timeout: 6000 })

    // The link drops once: the page keeps its text and the next request starts over.
    model.fail = true
    await wait(() => model.log.some(l => l.failed), 'a failed request', 6000)
    model.fail = false
    const failedAt = model.log.length
    await wait(() => model.log.length > failedAt, 'a request after the failure', 8000)
    assert.equal(model.log[failedAt].since, '0:0', 'after an error the page asks from the start')
    assert.match(await page.locator('pre').filter({ hasText: 'homing ,,,' }).first().innerText(), /homing ,,,/)
    await page.close()
  }

  // 2 — an older CC2 Control ignores the parameter and never says "unchanged": the page works as before.
  {
    const model = makeConsole('old')
    const page = await open()
    await page.route(consolePattern, model.handle)
    await page.goto(`${origin}#console`)
    await page.locator('pre').filter({ hasText: 'ready' }).first().waitFor({ timeout: 10000 })
    await wait(() => model.log.length >= 3, 'three polls of the older server', 12000)
    assert.ok(model.log.every(l => l.since === '0:0' && !l.unchanged), JSON.stringify(model.log))
    model.generation = 6
    model.output = 'second\n'
    await page.locator('pre').filter({ hasText: 'second' }).first().waitFor({ timeout: 6000 })
    await page.close()
  }

  // 2b — a backend that knows the parameter but names no instance (the first build of it) is never trusted for "unchanged",
  // and a backend that answers "unchanged" to the empty start value has nothing to show: the page works and asks in full.
  {
    const model = makeConsole('noinstance')
    model.generation = 0
    model.output = ''
    model.command = ''
    const page = await open()
    await page.route(consolePattern, model.handle)
    await page.goto(`${origin}#console`)
    await wait(() => model.log.length >= 2, 'two polls of a backend without instance', 8000)
    model.generation = 3
    model.output = 'hello\n'
    await page.locator('pre').filter({ hasText: 'hello' }).first().waitFor({ timeout: 8000 })
    await wait(() => model.log.length >= 5, 'more polls', 12000)
    assert.ok(model.log.slice(-3).every(l => l.since === '0:0' && !l.unchanged), JSON.stringify(model.log.slice(-3)))
    assert.match(await page.locator('pre').filter({ hasText: 'hello' }).first().innerText(), /hello/)
    await page.close()
  }

  // 2c — CC2 Control restarts between two polls without any request failing. The new instance reaches the same
  // generation and the same output length with another text: the open page must show the new text (and recompute what it
  // derives from it), not keep the old one because the two numbers match.
  {
    const model = makeConsole()
    const page = await open()
    await page.route(consolePattern, model.handle)
    await page.goto(`${origin}#console`)
    const log = page.locator('pre').filter({ hasText: 'ready' })
    await log.first().waitFor({ timeout: 10000 })
    await wait(() => model.log.filter(l => l.unchanged).length >= 2, 'two unchanged answers', 12000)
    model.instance = INSTANCE_B
    model.output = 'rdy!!\n' // 6 bytes like 'ready\n', the same generation 5
    await page.locator('pre').filter({ hasText: 'rdy!!' }).first().waitFor({ timeout: 8000 })
    assert.equal(model.log.filter(l => l.failed).length, 0, 'no request failed on the way')
    const askedOldInstance = model.log.filter(l => l.since === `5:6:${INSTANCE_A}`)
    assert.ok(!askedOldInstance.at(-1).unchanged, 'the request that named the old instance was answered in full')
    await wait(() => model.log.slice(-4).some(l => l.since === `5:6:${INSTANCE_B}` && l.unchanged), 'unchanged within the new instance', 12000)
    assert.match(await page.locator('pre').filter({ hasText: 'rdy!!' }).first().innerText(), /rdy!!/)
    await page.close()
  }

  // 2d — the same on the screw view, with the counters of the page itself: after a measurement started from this page it
  // ignores results of older generations. Then the service goes away for a moment and starts again with a lower generation
  // counter and other numbers: its result must come through, so what the page kept of the old console is forgotten, also
  // when a request failed on the way.
  {
    const model = makeConsole()
    model.generation = 9
    model.command = 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n'
    model.output = measureOutput
    const controls = []
    const page = await open()
    await page.route(consolePattern, model.handle)
    await page.route('**/api/screws/history', route => (route.request().method() === 'GET' ? route.fulfill({ json: { sessions: [] } }) : route.fallback()))
    await page.route('**/api/control', route => {
      const action = route.request().postData()
      controls.push(action)
      if (action === 'screws:measure') {
        model.busy = true
        model.completed = false
        model.success = false
        model.generation += 1 // 10: the page now ignores results before it
        model.output = ''
      }
      return route.fulfill({ status: 202, json: { accepted: true } })
    })
    await page.goto(`${origin}#bed/screws`)
    const card = page.locator('#screwFocus')
    await card.getByText('Measured results', { exact: true }).waitFor({ timeout: 10000 })
    const nudge = () => page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // look now, not in 20 s
    await card.getByRole('button', { name: 'Measure the four screws', exact: true }).click()
    await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
    await wait(() => controls.includes('screws:measure'), 'the measurement is sent', 8000)
    model.busy = false
    model.completed = true
    model.success = true
    model.output = measureOutput
    await card.getByText('Measured results', { exact: true }).waitFor({ timeout: 10000 })
    assert.equal(Buffer.byteLength(measureOutput), Buffer.byteLength(measureOutputRestarted))
    const before = await card.innerText()
    // The link is down for one request, then the service is a new instance: generation 3 is lower than the 10 the page waits for.
    model.fail = true
    await nudge()
    await wait(() => model.log.some(l => l.failed), 'a failed request', 6000)
    model.fail = false
    model.instance = INSTANCE_B
    model.generation = 3
    model.output = measureOutputRestarted
    let after = before
    for (let i = 0; i < 25 && after === before; i++) {
      await nudge()
      await sleep(400)
      after = await card.innerText()
    }
    assert.notEqual(after, before, 'the values of the restarted console are on the page, not the old ones')
    await page.close()
  }

  // 3 — the screw view: a slow look while nothing runs, a close one while a measurement runs and just after.
  {
    const model = makeConsole()
    model.generation = 9
    model.command = ''
    model.output = ''
    model.completed = false
    model.success = false
    const requests = { console: [], history: [], control: [] }
    let step = 0
    const page = await open()
    await page.clock.install()
    await page.route(consolePattern, route => {
      if (route.request().method() === 'GET') requests.console.push(step)
      return model.handle(route)
    })
    await page.route('**/api/screws/history', route => {
      if (route.request().method() !== 'GET') return route.fallback()
      requests.history.push(step)
      return route.fulfill({ json: { sessions: [] } })
    })
    await page.route('**/api/control', route => {
      const action = route.request().postData()
      requests.control.push(action)
      if (action === 'screws:measure') {
        model.busy = true
        model.completed = false
        model.success = false
        model.generation += 1
        model.command = 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n'
        model.output = ''
      }
      return route.fulfill({ status: 202, json: { accepted: true } })
    })
    await page.goto(`${origin}#bed/screws`)
    const card = page.locator('#screwFocus')
    await card.waitFor()
    await wait(() => requests.console.length >= 1 && requests.history.length >= 1, 'the first requests', 10000)
    await sleep(300)
    await page.clock.pauseAt(Date.now() + 1000)
    const settle = () => sleep(40)
    const tick = async ms => {
      for (let left = ms; left > 0; left -= 250) {
        await page.clock.runFor(Math.min(250, left))
        step += 1
        await settle()
      }
    }
    const idleStart = step
    await tick(70000) // 280 steps of 250 ms
    for (const [name, list] of Object.entries({ console: requests.console, history: requests.history })) {
      const inside = list.filter(s => s > idleStart)
      assert.ok(inside.length <= 4, `${name}: at most one request every 20 s while idle, got ${inside.length} in 70 s`)
      assert.ok(inside.length >= 2, `${name}: it still looks now and then, got ${inside.length}`)
      const gaps = inside.slice(1).map((s, i) => s - inside[i])
      assert.ok(gaps.every(g => g >= 79), `${name}: requests are at least 20 s apart: ${gaps}`) // 80 steps of 250 ms
    }
    assert.equal(await card.getByText('No measurement', { exact: true }).count(), 1, 'nothing measured yet')

    // Start a measurement: the page looks closely while it runs.
    await card.getByRole('button', { name: 'Measure the four screws', exact: true }).click()
    await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
    await wait(() => requests.control.includes('screws:measure'), 'the measurement is sent', 8000)
    const busyStart = step
    const consoleBefore = requests.console.length
    await tick(1000)
    await card.getByText('Probing…', { exact: true }).waitFor({ timeout: 5000 })
    await tick(9000)
    const running = requests.console.slice(consoleBefore).filter(s => s >= busyStart)
    assert.ok(running.length >= 3, `a running measurement is looked at every 2.5 s, got ${running.length} in 10 s`)
    assert.equal(model.log.filter(l => l.busy && l.unchanged).length, 0, 'a running command never gets "unchanged"')

    // It ends: the result is on the page within 3 s, the saved trail is fetched, then the slow look returns.
    model.busy = false
    model.completed = true
    model.success = true
    model.output = measureOutput
    const doneStep = step
    const historyBefore = requests.history.length
    let shown = false
    for (let i = 0; i < 12 && !shown; i++) {
      await tick(250)
      shown = (await card.getByText('Measured results', { exact: true }).count()) === 1
    }
    assert.ok(shown, 'the result appears within 3 s of the end')
    assert.ok((step - doneStep) * 250 <= 3000, 'the result needs at most 3 s')
    await tick(2000)
    assert.ok(requests.history.length > historyBefore, 'the saved trail is fetched after the measurement')
    await tick(20000) // the close look ends 10 s after the command
    const calmStart = step
    const calmConsole = requests.console.length
    const calmHistory = requests.history.length
    await tick(40000)
    assert.ok(requests.console.length - calmConsole <= 3, 'the slow look returns after the measurement (console)')
    assert.ok(requests.history.length - calmHistory <= 3, 'the slow look returns after the measurement (trail)')
    assert.ok(step > calmStart)
    assert.ok(
      model.log.slice(-6).some(l => l.unchanged),
      'while idle the answers are short: a console that did not change is not sent again'
    )
    await page.close()
  }
  assert.deepEqual(errors, [])
  console.log('PASS console poll: since/unchanged, text kept, older server and a backend without instance, restart of the service, screw view idle cadence and result within 3 s')
} finally {
  await browser?.close()
  await server.close()
}
