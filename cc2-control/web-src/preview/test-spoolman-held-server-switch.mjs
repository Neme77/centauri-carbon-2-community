import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
import path from 'node:path'

// The list "Usage not sent to Spoolman" when the Spoolman server changes on the same page, without a reload.
// Two servers with overlapping spool numbers (7 is a different spool on each): a loaded catalog and the targets chosen
// from it belong to the server they were read from. After a change of server nothing of the old server may be offered
// or sent: not a catalog, not a chosen number, not a number that only looks like the same spool. The fake printer
// answers like the real one: it tags the list with the generation of the server settings, ends "login_changed" for a
// new server and refuses a send that carries an older generation. Fake data only (no printer, no Spoolman).
const root = fileURLToPath(new URL('..', import.meta.url))
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const origin = server.resolvedUrls.local[0]
  const at = 1790000000
  const servers = {
    A: { url: 'http://192.0.2.5:7912', gen: 41, spools: [
      { id: '7', name: 'Alpha Red', material: 'PLA', color: '#CC2222', remaining: 800 },
      { id: '8', name: 'Alpha Blue', material: 'PETG', color: '#2222CC', remaining: 400.5 },
    ] },
    B: { url: 'http://192.0.2.9:7912', gen: 42, spools: [
      { id: '7', name: 'Beta Green', material: 'PLA', color: '#22CC22', remaining: 650 },
      { id: '9', name: 'Beta Black', material: 'ABS', color: '#101010', remaining: 300 },
    ] },
    C: { url: 'http://192.0.2.13:7912', gen: 43, spools: [{ id: '7', name: 'Gamma Grey', material: 'PLA', color: '#888888', remaining: 500 }] },
  }
  let current = 'A'
  let switched = false // the address changed since the entry was written: the real printer ends "login_changed" then
  const reasonOf = r => (r === 'login_changed' && switched ? 'server_changed' : r)
  const entry = (id, name, grams, reason, spoolmanId) => ({ id, spool: 'aa0000000000000' + id, name, grams, reason, spoolman_id: spoolmanId, time: at - 86400, sending: false })
  const heldBase = [entry(1, 'Black PLA', 12.5, 'login_changed', '7'), entry(2, 'Green PETG', 4.0, 'not_found', '9')]
  const sends = [] // every send request: what the page sent and whether the printer accepted it
  const gates = {} // a catalog read of this server is held until the gate opens
  const errors = []
  const context = await browser.newContext({ viewport: { width: 1440, height: 1400 } })
  const page = await context.newPage()
  page.on('pageerror', e => errors.push(e.message))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/spools', async r => {
    const response = await r.fetch()
    const data = await response.json()
    Object.assign(data, {
      spoolman_enabled: true, spoolman_url: servers[current].url, spoolman_has_auth: false, spoolman_stale_links: 0,
      spoolman_gen: servers[current].gen, spoolman_held: heldBase.map(e => ({ ...e, reason: reasonOf(e.reason) })),
    })
    await r.fulfill({ json: data })
  })
  await page.route('**/api/spoolman/catalog/refresh', r => r.fulfill({ status: 202, json: { ok: true } }))
  await page.route('**/api/spoolman/catalog', async r => {
    const from = current // the server that answers is the one configured when the request arrived
    if (gates[from]) await gates[from].promise
    await r.fulfill({ json: { loading: false, error: '', gen: servers[from].gen, spools: servers[from].spools } })
  })
  await page.route('**/api/spoolman/held/send', r => {
    const body = r.request().postData() || ''
    const gen = Number((body.split('\n').find(l => l.startsWith('gen=')) || '').slice(4))
    const accepted = gen === servers[current].gen
    sends.push({ body, accepted })
    return accepted
      ? r.fulfill({ status: 202, json: { ok: true } })
      : r.fulfill({ status: 409, json: { error: 'The Spoolman server changed since the list was loaded: reload the list and choose again' } })
  })
  await page.addInitScript(() => localStorage.setItem('cc2-spoolman-held-seen', '99'))

  const row = id => page.locator(`[data-held-id="${id}"]`)
  const names = id => row(id).getByRole('combobox').locator('option').allTextContents()
  const value = id => row(id).getByRole('combobox').inputValue()
  const sendButton = page.getByRole('button', { name: /^Send( \d+ selected|ing| selected)/ })
  // The printer's own polling would take 20 s: coming back to the tab refreshes the page straight away.
  const poll = () => page.evaluate(() => document.dispatchEvent(new Event('visibilitychange')))
  const serverChanged = 'You changed the Spoolman address. This spool is connected to a spool of the old address. Choose the matching spool on the new Spoolman.'
  const loginChanged = 'You changed the login to Spoolman. The spool is the same, so you can send this amount to it.'
  const switchTo = name => { current = name; switched = true }
  const heading = page.getByRole('heading', { name: 'Usage not sent to Spoolman (2 entries)', exact: true })

  // 1. Server A: catalog loaded; the login entry starts on its own spool 7; entry 2 gets A's spool 7 chosen (a number B also has).
  await page.goto(origin + '#spools')
  await heading.waitFor()
  await page.waitForFunction(() => document.querySelectorAll('[data-held-id="2"] option').length === 3)
  assert.deepEqual(await names(2), ['Choose a spool', 'Spoolman #7 · Alpha Red · PLA · 800.0 g', 'Spoolman #8 · Alpha Blue · PETG · 400.5 g'])
  assert.ok((await row(1).innerText()).includes(loginChanged))
  assert.equal(await value(1), '7', 'a login change keeps its own spool')
  await row(2).getByRole('combobox').selectOption('7')
  await row(1).getByRole('checkbox').check()
  await row(2).getByRole('checkbox').check()
  assert.equal(await sendButton.innerText(), 'Send 2 selected (16.5 g)')
  assert.equal(await sendButton.isDisabled(), false)

  // 2. The address changes to server B, with no reload. Nothing of A stays: no catalog, no chosen number, no tick,
  // and the login entry no longer says "the spool is the same".
  switchTo('B')
  await poll()
  await page.waitForFunction(() => document.querySelectorAll('[data-held-id="2"] option').length === 3 &&
    !document.querySelector('[data-held-id="2"]').innerText.includes('Alpha'))
  assert.deepEqual(await names(2), ['Choose a spool', 'Spoolman #7 · Beta Green · PLA · 650.0 g', 'Spoolman #9 · Beta Black · ABS · 300.0 g'])
  assert.deepEqual(await names(1), ['Choose a spool', 'Spoolman #7 · Beta Green · PLA · 650.0 g', 'Spoolman #9 · Beta Black · ABS · 300.0 g'])
  assert.equal(await value(2), '', 'A\'s spool 7 is not carried over to B\'s spool 7')
  assert.equal(await value(1), '', 'the old number is no default on the new server')
  assert.ok((await row(1).innerText()).includes(serverChanged))
  assert.ok(!(await row(1).innerText()).includes(loginChanged))
  assert.equal(await row(1).getByRole('checkbox').isChecked(), false, 'the ticks of the old server are gone')
  assert.equal(await row(2).getByRole('checkbox').isChecked(), false)
  assert.equal(await sendButton.innerText(), 'Send selected')
  assert.equal(await sendButton.isDisabled(), true)
  assert.equal(sends.length, 0, 'nothing was sent')
  assert.equal(await page.getByText('Alpha').count(), 0, 'no spool name of A anywhere on the page')

  // 3. A is read again while the change happens: A's answer arrives after the page has already switched to B.
  switched = false
  current = 'A'
  let open
  gates.A = { promise: new Promise(done => { open = done }) }
  await page.reload() // a new start of the scenario, not part of the switch below
  await heading.waitFor()
  await page.waitForFunction(() => document.querySelector('[data-held-id="1"]'))
  assert.deepEqual(await names(2), ['Choose a spool'], 'A\'s read is still on its way')
  switchTo('B')
  await poll()
  await page.waitForFunction(() => document.querySelectorAll('[data-held-id="2"] option').length === 3)
  assert.deepEqual(await names(2), ['Choose a spool', 'Spoolman #7 · Beta Green · PLA · 650.0 g', 'Spoolman #9 · Beta Black · ABS · 300.0 g'])
  open() // now the late answer of A comes in
  await page.waitForTimeout(1200)
  assert.deepEqual(await names(2), ['Choose a spool', 'Spoolman #7 · Beta Green · PLA · 650.0 g', 'Spoolman #9 · Beta Black · ABS · 300.0 g'], 'the late answer of A does not fill the picker')
  assert.deepEqual(await names(1), ['Choose a spool', 'Spoolman #7 · Beta Green · PLA · 650.0 g', 'Spoolman #9 · Beta Black · ABS · 300.0 g'])
  assert.equal(await page.getByText('Alpha').count(), 0)
  assert.equal(sends.length, 0)
  // Choices on B go to B with B's generation.
  await row(2).getByRole('combobox').selectOption('7')
  await row(2).getByRole('checkbox').check()
  await sendButton.click()
  await page.waitForFunction(() => document.body.innerText.includes('Sent to Spoolman.') || document.body.innerText.includes('Some amounts could not be sent'))
  assert.deepEqual(sends, [{ body: `gen=${servers.B.gen}\n2=7`, accepted: true }], 'one send: B\'s generation, the spool chosen from B\'s catalog')

  // 4. The server changes between the click and the page's next look: the printer refuses the send; nothing is applied.
  // (The page cannot know yet, so it still sends B's generation; the refusal must come from the printer.)
  await page.reload()
  await heading.waitFor()
  await page.waitForFunction(() => document.querySelectorAll('[data-held-id="2"] option').length === 3)
  await row(2).getByRole('combobox').selectOption('7')
  await row(2).getByRole('checkbox').check()
  sends.length = 0
  current = 'C'
  await sendButton.click()
  await page.getByText('The Spoolman address or login changed while you were choosing. The list was updated and nothing was sent. Choose the spools again.', { exact: true }).waitFor()
  assert.equal(sends.length, 1)
  assert.equal(sends[0].accepted, false, 'a selection made under another server is refused, not read against the new one')
  assert.equal(sends[0].body, `gen=${servers.B.gen}\n2=7`)
  // The refusal brings the page up to date: C's list is read, the old choice is gone.
  await page.waitForFunction(() => document.querySelectorAll('[data-held-id="2"] option').length === 2 &&
    document.querySelector('[data-held-id="2"]').innerText.includes('Gamma Grey'))
  assert.equal(await value(2), '')
  assert.equal(await row(2).getByRole('checkbox').isChecked(), false)
  assert.equal(await sendButton.isDisabled(), true)

  assert.deepEqual(errors, [])
  console.log('PASS: Spoolman list after a change of server without reload: catalog, chosen spools and ticks are dropped, the login default ends, a late answer of the old server is ignored, a send under an old generation is refused')
} finally {
  await browser?.close()
  await server.close()
}
