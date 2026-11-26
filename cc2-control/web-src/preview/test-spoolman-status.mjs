import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
import path from 'node:path'

// The Spoolman card tells what the last sync did: every result, spools without a weight, a full library, "not yet", and
// nothing when the sync is off. Refusals come in the page's language. Fake data only (no printer, no Spoolman).
const root = fileURLToPath(new URL('..', import.meta.url))
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const origin = server.resolvedUrls.local[0]
  const at = 1790000000
  const errors = []
  const status = (over = {}, list = {}) => ({
    enabled: true, configured: true, last_sync: at, result: 'ok', http: 0, unsent_spools: 0, unsent_grams: 0, inflight_spools: 0,
    held_entries: 0, held_grams: 0, list: { pages: 1, read: 3, new: 0, unreadable: 0, no_weight: 0, library_full: 0, too_large: 0, no_material: 0, no_colour: 0, ...list },
    archived_linked: 0, linked_no_weight: 0, orphaned: [], orphaned_count: 0, ...over,
  })
  let current = status()
  let language = 'en'
  let held = []
  let catalogSpools = [{ id: '7', name: 'Black PLA', material: 'PLA', color: '#101010', remaining: 810.4 }]
  const counts = { status: 0, post: 0, refresh: 0 }
  let postAnswer = { status: 200, json: { ok: true } }
  const context = await browser.newContext({ viewport: { width: 1440, height: 1400 }, locale: 'de-DE' })
  const page = await context.newPage()
  page.on('pageerror', e => errors.push(e.message))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language, theme: 'dark' } }))
  await page.route('**/api/spools', async r => {
    const response = await r.fetch()
    const data = await response.json()
    Object.assign(data, { enabled: true, spoolman_enabled: true, spoolman_url: 'http://192.0.2.5:7912', spoolman_has_auth: false, spoolman_gen: 5, spoolman_stale_links: 0, spoolman_held: held })
    await r.fulfill({ json: data })
  })
  await page.route('**/api/spools/enable', r => r.fulfill({ json: { ok: true } }))
  await page.route('**/api/spoolman', r => {
    if (r.request().method() === 'GET') { counts.status++; return r.fulfill({ json: current }) }
    counts.post++
    return r.fulfill(postAnswer)
  })
  await page.route('**/api/spoolman/catalog/refresh', r => { counts.refresh++; return r.fulfill({ status: 202, json: { ok: true } }) })
  await page.route('**/api/spoolman/catalog', r => r.fulfill({ json: { gen: 5, loading: false, error: '', spools: catalogSpools } }))

  const line = page.locator('[data-spoolman-status]')
  const open = async () => {
    await page.goto(origin + '#spools')
    await page.getByRole('heading', { name: /Spoolman/ }).first().waitFor()
  }
  const when = s => page.evaluate(t => new Date(t * 1000).toLocaleString([], { day: '2-digit', month: '2-digit', hour: '2-digit', minute: '2-digit' }), s)
  const time = await when(at)
  const amber = async () => (await line.getAttribute('class')).includes('border-amber')
  const check = async (name, over, list, text, hint, warn) => {
    current = status(over, list)
    await page.reload()
    await line.waitFor({ timeout: 8000 })
    const got = (await line.innerText()).replace(/^[✓⚠]\s*/, '').split('\n')
    assert.deepEqual(got, hint ? [text, hint] : [text], name)
    assert.equal(await amber(), warn, `${name}: colour`)
    assert.equal(await line.count(), 1, `${name}: exactly one line`)
  }

  // English first: every result, in rank order.
  await open()
  await check('ok', {}, {}, `Synchronised with Spoolman. Last sync: ${time}.`, null, false)
  await check('never', { last_sync: 0, result: 'never' }, {}, 'Not synchronised yet. The first sync runs within two minutes.', null, false)
  await check('bad_url', { result: 'bad_url' }, {}, 'The address is not valid.', 'Use an IPv4 address, for example http://192.168.1.50:7912. Names are not resolved.', true)
  await check('unauthorized', { result: 'unauthorized', http: 401 }, {}, 'Spoolman refused the login.', 'Check the login of the reverse proxy.', true)
  await check('forbidden', { result: 'forbidden', http: 403 }, {}, 'Spoolman refused the login.', 'Check the login of the reverse proxy.', true)
  await check('unreachable', { result: 'unreachable' }, {}, 'Spoolman cannot be reached.', 'Check the address and that Spoolman is running.', true)
  await check('timeout', { result: 'timeout' }, {}, 'Spoolman did not answer in time.', 'Spoolman or the network may be busy. The next try follows by itself.', true)
  await check('bad_reply', { result: 'bad_reply' }, {}, 'Spoolman sent an incomplete reply.', 'Check that the address leads to Spoolman and not to another service.', true)
  await check('too_large', { result: 'too_large' }, {}, 'Spoolman sent more data than the printer can read.', 'Shorten very long notes in Spoolman or archive spools you no longer use.', true)
  await check('not_found', { result: 'not_found', http: 404 }, {}, 'Spoolman does not know this address (HTTP 404).', 'Check the path after the port.', true)
  await check('server_error', { result: 'server_error', http: 503 }, {}, 'Spoolman reports an error (HTTP 503).', null, true)
  await check('refused', { result: 'refused', http: 418 }, {}, 'Spoolman refused the request (HTTP 418).', null, true)
  await check('no_weight', {}, { no_weight: 3 }, `Synchronised (${time}). Spools in Spoolman without a weight were not added: 3.`, 'Enter the weight of these spools in Spoolman.', true)
  await check('library_full', {}, { library_full: 5 }, `Synchronised (${time}). The spool library is full; spools from Spoolman that were not added: 5.`, 'Delete spools here that you no longer need.', true)
  await check('no_weight before library_full', {}, { no_weight: 2, library_full: 5 }, `Synchronised (${time}). Spools in Spoolman without a weight were not added: 2.`, 'Enter the weight of these spools in Spoolman.', true)
  await check('a failed sync before no_weight', { result: 'timeout' }, { no_weight: 2 }, 'Spoolman did not answer in time.', 'Spoolman or the network may be busy. The next try follows by itself.', true)

  // The time is written out, not "3 minutes ago".
  current = status()
  await page.reload(); await line.waitFor()
  assert.ok(!/ago|vor /.test(await line.innerText()))

  // Off, or no address: no line. The note about IPv4 stays under the address, always.
  const hint = 'Only IPv4 addresses, for example http://192.168.1.50:7912. Names such as “spoolman” are not resolved.'
  for (const over of [{ enabled: false }, { configured: false }]) {
    current = status(over)
    await page.reload()
    await page.getByText(hint, { exact: true }).waitFor()
    await page.waitForTimeout(400)
    assert.equal(await line.count(), 0, `no line for ${JSON.stringify(over)}`)
  }
  const input = page.getByPlaceholder('http://192.168.1.50:7912')
  const box = await input.boundingBox(), note = await page.getByText(hint, { exact: true }).boundingBox()
  assert.ok(note.y >= box.y + box.height - 1 && note.y < box.y + box.height + 30, 'the note stands right below the address field')

  // A narrow phone: no horizontal scrolling, with the longest line and hint.
  current = status({ result: 'too_large' })
  await page.setViewportSize({ width: 390, height: 900 })
  await page.reload(); await line.waitFor()
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth), '390 px: no horizontal scrolling')
  const lb = await line.boundingBox()
  assert.ok(lb.x >= 0 && lb.x + lb.width <= 390, '390 px: the line fits the screen')
  await page.setViewportSize({ width: 1440, height: 1400 })

  // Save: the printer's answer is read again at once, and a refusal is shown in the page's language (English text without a code).
  current = status()
  await page.reload(); await line.waitFor()
  let before = counts.status
  await page.getByRole('button', { name: 'Save', exact: true }).click()
  await page.getByText('Spoolman settings saved.').waitFor()
  await page.waitForFunction(() => true)
  await page.waitForTimeout(300)
  assert.ok(counts.status > before, 'Save reads the status again at once')
  const bad = { status: 400, json: { ok: false, error: 'Invalid URL: use http://IPv4-address[:port][/path] (host names are not resolved, no TLS)', code: 'spoolman_bad_url' } }
  postAnswer = bad
  await page.getByRole('button', { name: 'Save', exact: true }).click()
  await page.getByText('Rejected: The address is not valid. Use http://IPv4-address[:port][/path]; names are not resolved and TLS is not supported.', { exact: true }).waitFor()
  postAnswer = { status: 409, json: { ok: false, error: 'A new refusal of a later version', code: 'spoolman_from_the_future' } }
  await page.getByRole('button', { name: 'Save', exact: true }).click()
  await page.getByText('Rejected: A new refusal of a later version', { exact: true }).waitFor()
  postAnswer = { status: 400, json: { ok: false, error: 'Invalid request' } }
  await page.getByRole('button', { name: 'Save', exact: true }).click()
  await page.getByText('Rejected: Invalid request', { exact: true }).waitFor()
  postAnswer = { status: 200, json: { ok: true } }

  // The second look 4 s after "Save": two quick saves leave one timer, and leaving the page leaves none.
  const save = () => page.getByRole('button', { name: 'Save', exact: true }).click()
  await page.reload(); await line.waitFor()
  await page.waitForTimeout(300)
  before = counts.status
  await save(); await save()
  await page.waitForTimeout(4800)
  assert.equal(counts.status - before, 3, `two saves: 2 reads at once + 1 after 4 s, got ${counts.status - before}`)
  await save()
  await page.waitForTimeout(300)
  before = counts.status
  await page.evaluate(() => { location.hash = '#files' })
  await page.waitForFunction(() => !document.querySelector('[data-spoolman-status]'))
  await page.waitForTimeout(4800)
  assert.equal(counts.status, before, 'leaving the page: no read after the 4 s')
  await open()

  // German: the line, its hint and the refusal.
  language = 'de'
  current = status({ result: 'bad_url' })
  await page.reload()
  await line.waitFor()
  assert.deepEqual((await line.innerText()).replace(/^[✓⚠]\s*/, '').split('\n'), ['Die Adresse ist ungültig.', 'Trage eine IPv4-Adresse ein, zum Beispiel http://192.168.1.50:7912. Namen werden nicht aufgelöst.'])
  await page.getByText('Nur IPv4-Adressen, zum Beispiel http://192.168.1.50:7912. Namen wie „spoolman“ werden nicht aufgelöst.', { exact: true }).waitFor()
  postAnswer = bad
  await page.getByRole('button', { name: 'Speichern', exact: true }).click()
  await page.getByText('Abgelehnt: Die Adresse ist ungültig. Nimm http://IPv4-Adresse[:Port][/Pfad]; Namen werden nicht aufgelöst, TLS wird nicht unterstützt.', { exact: true }).waitFor()
  postAnswer = { status: 200, json: { ok: true } }
  current = status({ result: 'ok' }, { library_full: 4 })
  await page.reload(); await line.waitFor()
  assert.ok((await line.innerText()).includes(`Abgeglichen (${time}). Die Spool-Bibliothek ist voll; nicht übernommene Spools aus Spoolman: 4.`))
  language = 'en'

  // Z1: many refreshes of the page cost no request to /api/spoolman of their own, and a list that cannot be read is tried
  // at most three more times, every try counted, however the sync goes.
  held = [{ id: 1, spool: 'aa00000000000001', name: 'Black PLA', grams: 12.5, reason: 'server_changed', spoolman_id: '7', time: at, sending: false }]
  catalogSpools = []
  current = status({ last_sync: at })
  counts.refresh = 0
  await page.reload()
  await page.getByRole('heading', { name: /Usage not sent to Spoolman/ }).waitFor()
  await page.getByRole('dialog').getByRole('button', { name: 'Review', exact: true }).click() // the notice of the list, once
  await page.waitForFunction(() => !document.querySelector('[role=dialog]'))
  await page.waitForTimeout(500)
  assert.equal(counts.refresh, 1, 'the empty list is asked for once')
  before = counts.status
  for (let i = 0; i < 6; i++) {
    await page.getByRole('button', { name: /Turn (on|off) spool tracking/ }).click() // every action refreshes the spool library
    await page.waitForTimeout(250)
  }
  assert.ok(counts.status - before <= 1, `6 refreshes of the page asked /api/spoolman ${counts.status - before} times`)
  assert.equal(counts.refresh, 1, 'a sync that has not moved on does not ask again')
  for (let i = 1; i <= 8; i++) {
    current = status({ last_sync: at + i * 120 })
    await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange')))
    await page.waitForTimeout(350)
  }
  assert.equal(counts.refresh, 4, 'one read and at most three more, after a good sync each')
  // A read that works fills the choice and starts the count again.
  catalogSpools = [{ id: '7', name: 'Black PLA', material: 'PLA', color: '#101010', remaining: 810.4 }]
  current = status({ last_sync: at + 5000 })
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange')))
  await page.waitForTimeout(400)
  assert.equal(counts.refresh, 4, 'after three tries it stays quiet until the page is reloaded')

  assert.deepEqual(errors, [])
  console.log('PASS: Spoolman status line: every result, no weight, library full, not yet, off = no line, 390 px, German, translated refusals, shared status with a counted limit of tries')
} finally {
  await browser?.close()
  await server.close()
}
