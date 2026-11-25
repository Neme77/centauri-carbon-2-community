import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { mkdir } from 'node:fs/promises'

// The list "Usage not sent to Spoolman": every reason, every button, with fake data only (no printer, no Spoolman).
const root = fileURLToPath(new URL('..', import.meta.url))
const shots = process.env.CC2_SHOTS || path.join(root, 'preview/screenshots')
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const origin = server.resolvedUrls.local[0]
  const at = 1790000000 // 2026-09-21
  const entry = (id, name, grams, reason, spoolmanId, days) => ({ id, spool: 'aa0000000000000' + id, name, grams, reason, spoolman_id: spoolmanId, time: at - days * 86400, sending: false })
  const fresh = () => ({
    enabled: true,
    stale: 3,
    held: [
      entry(1, 'Black PLA (Rack 1)', 12.5, 'login_changed', '7', 2),
      entry(2, 'Silk Blue', 29.1, 'server_changed', '8', 3),
      entry(3, 'Green PLA', 4.0, 'not_found', '9', 5),
      entry(4, 'Grey PETG', 6.2, 'unsure', '10', 4),
    ],
  })
  let state = fresh()
  const writes = [], errors = []
  const catalog = {
    gen: 5, loading: false, error: '',
    spools: [
      { id: '7', name: 'Black PLA', material: 'PLA', color: '#101010', remaining: 810.4 },
      { id: '21', name: 'Silk Blue', material: 'PLA Silk', color: '#2244CC', remaining: 640 },
      { id: '22', name: 'Green PLA', material: 'PLA', color: '#22AA44', remaining: 120.5 },
    ],
  }
  // The browser speaks French on purpose: dates follow the language chosen on the page, not the browser's.
  const context = await browser.newContext({ viewport: { width: 1440, height: 1400 }, locale: 'fr-FR' })
  const page = await context.newPage()
  page.on('pageerror', e => errors.push(e.message))
  let language = 'en'
  await page.route('**/api/preferences', r => r.fulfill({ json: { language, theme: 'dark' } }))
  await page.route('**/api/spools', async r => {
    const response = await r.fetch()
    const data = await response.json()
    Object.assign(data, { spoolman_enabled: state.enabled, spoolman_url: 'http://192.0.2.5:7912', spoolman_has_auth: false, spoolman_gen: 5, spoolman_stale_links: state.stale, spoolman_held: state.held })
    await r.fulfill({ json: data })
  })
  await page.route('**/api/spoolman/catalog/refresh', r => r.fulfill({ status: 202, json: { ok: true } }))
  await page.route('**/api/spoolman/catalog', r => r.fulfill({ json: catalog }))
  await page.route('**/api/spoolman/keep-old', r => {
    writes.push(['keep', r.request().postData() || ''])
    state.stale = 0
    return r.fulfill({ json: { ok: true } })
  })
  await page.route('**/api/spoolman/held/send', r => {
    const body = r.request().postData() || ''
    writes.push(['send', body])
    const ids = body.split('\n').map(l => Number(l.split('=')[0]))
    state.held = state.held.filter(e => !ids.includes(e.id))
    return r.fulfill({ status: 202, json: { ok: true } })
  })
  await page.route('**/api/spoolman/held/discard', r => {
    const body = r.request().postData() || ''
    writes.push(['discard', body])
    state.held = state.held.filter(e => `id=${e.id}` !== body)
    return r.fulfill({ json: { ok: true } })
  })
  await page.route('**/api/spoolman/unlink-old', r => {
    writes.push(['unlink', r.request().postData() || ''])
    state.stale = 0
    return r.fulfill({ json: { ok: true } })
  })
  const row = id => page.locator(`[data-held-id="${id}"]`)
  const open = async () => { await page.goto(origin + '#spools'); await page.getByRole('heading', { name: /Usage not sent to Spoolman|Spoolman sync/ }).first().waitFor() }

  // 1. After a change of server or login the page says so once; "Review" takes it away for good.
  await page.goto(origin + '#spools')
  const notice = page.getByRole('dialog')
  await notice.getByText('Some filament use has not been sent to Spoolman', { exact: true }).waitFor()
  await notice.getByText('CC2 Control tells Spoolman how much filament you use. Since your change, some amounts have not been sent. Spoolman therefore shows too much filament left. Nothing is sent until you choose. Nothing is thrown away by this change.', { exact: true }).waitFor()
  assert.equal(await notice.getByText('No amount is lost').count(), 0, 'the notice does not promise more than it can keep')
  await notice.getByRole('button', { name: 'Review', exact: true }).click()
  await page.waitForFunction(() => !document.querySelector('[role=dialog]'))
  await page.reload()
  await page.getByRole('heading', { name: 'Usage not sent to Spoolman (4 entries)', exact: true }).waitFor()
  assert.equal(await page.getByRole('dialog').count(), 0, 'the notice is shown once')

  // 2. The list: title, introduction, every reason with its own text, and the buttons that belong to it.
  await page.getByText('These amounts of filament were used on this printer, but Spoolman does not know about them yet. Choose what to do with each one. Nothing is sent until you press "Send selected".', { exact: true }).waitFor()
  const reasons = {
    1: 'You changed the login to Spoolman. The spool is the same, so you can send this amount to it.',
    2: 'You changed the Spoolman address. This spool is connected to a spool of the old address. Choose the matching spool on the new Spoolman.',
    3: 'Spoolman does not have this spool any more. It may have been deleted there. Choose another spool, or do not send this amount.',
    4: 'This amount was sent before, but Spoolman’s answer never arrived. Spoolman may already have counted it. It is not sent a second time. You can only remove this entry from the list. If in doubt, check the weight of this spool in Spoolman.',
  }
  for (const [id, text] of Object.entries(reasons)) assert.ok((await row(id).innerText()).includes(text), `reason text of entry ${id}`)
  assert.ok((await row(1).innerText()).includes('12.5 g'))
  for (const id of [1, 2, 3]) {
    assert.equal(await row(id).getByRole('button', { name: 'Don’t send', exact: true }).count(), 1)
    assert.equal(await row(id).getByRole('combobox').count(), 1)
    assert.equal(await row(id).getByRole('checkbox').count(), 1)
  }
  assert.equal(await row(4).getByRole('button', { name: 'Don’t send', exact: true }).count(), 0, '"Don’t send" is not offered for an unsure entry')
  assert.equal(await row(4).getByRole('button', { name: 'Remove from list', exact: true }).count(), 1)
  assert.equal(await row(4).getByRole('combobox').count(), 0)
  assert.equal(await row(4).getByRole('checkbox').count(), 0, 'an unsure entry can only be removed')
  // The choice shows name, material and the weight Spoolman has for the spool; a login change starts on its own spool.
  const options = await row(2).getByRole('combobox').locator('option').allTextContents()
  assert.deepEqual(options, ['Choose a spool', 'Black PLA · PLA · 810.4 g', 'Silk Blue · PLA Silk · 640.0 g', 'Green PLA · PLA · 120.5 g'])
  // Each choice is named after its entry, so a screen reader can tell the rows apart.
  assert.equal(await row(2).getByRole('combobox', { name: 'Choose a spool: Silk Blue', exact: true }).count(), 1)
  assert.equal(await row(1).getByRole('combobox', { name: 'Choose a spool: Black PLA (Rack 1)', exact: true }).count(), 1)
  // The date is written in the language of the page (English here), not in the browser's (French).
  const day = (lang, entryTime) => new Date(entryTime * 1000).toLocaleDateString(lang, { day: 'numeric', month: 'short' })
  assert.notEqual(day('en', at - 2 * 86400), day('fr-FR', at - 2 * 86400))
  assert.ok((await row(1).innerText()).includes(day('en', at - 2 * 86400)), 'English date')
  assert.equal(await row(1).getByRole('combobox').inputValue(), '7')
  assert.equal(await row(2).getByRole('combobox').inputValue(), '')
  assert.equal(await page.getByRole('button', { name: 'Send selected', exact: true }).isDisabled(), true, 'nothing is selected: nothing to send')
  assert.equal(writes.length, 0, 'nothing is sent by itself')

  // 3. The line above after a change of address.
  const stale = page.locator('[data-held-stale]')
  await stale.getByText('3 spools on this printer are still connected to the old Spoolman address.', { exact: true }).waitFor()
  await stale.getByText('The spools stay. Only the connection to Spoolman is removed, so no wrong spool gets an update. You can connect them again later. At the next sync, Spoolman’s spools are added here as new spools, so some spools may appear twice.', { exact: true }).waitFor()
  await stale.getByText('Keep them if this is the same Spoolman under a new address. Until you choose, nothing from Spoolman is copied to these spools and no usage is sent to Spoolman. New usage is collected and handled after your choice.', { exact: true }).waitFor()
  assert.deepEqual(await stale.getByRole('button').allTextContents(), ['Keep connections', 'Remove old connections'])

  // 4. Send selected is locked until every chosen entry has a spool, and says why.
  await row(1).getByRole('checkbox').check()
  await row(2).getByRole('checkbox').check()
  const send = page.getByRole('button', { name: /^Send \d+ selected/ })
  assert.equal(await send.innerText(), 'Send 2 selected (41.6 g)')
  assert.equal(await send.isDisabled(), true)
  await page.getByText('Choose a spool for each selected amount.', { exact: true }).waitFor()
  await row(2).getByRole('combobox').selectOption('21')
  assert.equal(await send.isDisabled(), false)
  assert.equal(await page.getByText('Choose a spool for each selected amount.').count(), 0)
  await page.getByText('2 selected').first().waitFor()

  // 5. Only the chosen entries are sent, each to the spool shown next to it; then they are gone from the list.
  await page.screenshot({ path: path.join(shots, 'held-1440-selected.png') })
  await send.click()
  await page.getByRole('heading', { name: 'Usage not sent to Spoolman (2 entries)', exact: true }).waitFor()
  assert.deepEqual(writes, [['send', 'gen=5\n1=7\n2=21']])
  assert.equal(await row(1).count() + (await row(2).count()), 0)
  assert.equal(await row(3).count(), 1)
  await page.getByText('Sent to Spoolman.', { exact: true }).waitFor()

  // 6. Don't send: a question with its text and two buttons; Cancel changes nothing; confirming removes the entry only.
  await row(3).getByRole('button', { name: 'Don’t send', exact: true }).click()
  const ask = page.getByRole('dialog')
  await ask.getByText('Don’t send 4.0 g? Spoolman will keep showing this amount as still on the spool. At the next sync, the weight of the spool on this printer can go back to Spoolman’s figure. This cannot be undone.', { exact: true }).waitFor()
  assert.deepEqual(await ask.getByRole('button').allTextContents(), ['Cancel', 'Don’t send'])
  await ask.getByRole('button', { name: 'Cancel', exact: true }).click()
  await page.waitForFunction(() => !document.querySelector('[role=dialog]'))
  assert.equal(writes.length, 1)
  assert.equal(await row(3).count(), 1)
  await row(3).getByRole('button', { name: 'Don’t send', exact: true }).click()
  await ask.getByRole('button', { name: 'Don’t send', exact: true }).click()
  await page.waitForFunction(() => !document.querySelector('[data-held-id="3"]'))
  assert.deepEqual(writes.slice(1), [['discard', 'id=3']], 'only the entry is removed; nothing is sent')

  // 7. Remove from list (unsure): its own short question; nothing is sent.
  await page.getByRole('heading', { name: 'Usage not sent to Spoolman (1 entry)', exact: true }).waitFor()
  await row(4).getByRole('button', { name: 'Remove from list', exact: true }).click()
  await ask.getByText('Remove this entry? Nothing is sent.', { exact: true }).waitFor()
  await ask.getByRole('button', { name: 'Remove from list', exact: true }).click()
  await page.waitForFunction(() => !document.querySelector('[data-held-id="4"]'))
  assert.deepEqual(writes.slice(2), [['discard', 'id=4']])
  assert.equal(writes.filter(w => w[0] === 'send').length, 1, 'only the one send the user pressed')

  // 8. Nothing waits: the list says so while old connections are left; the button removes them (the spools stay).
  await page.getByText('Nothing is waiting. All amounts have been sent to Spoolman.', { exact: true }).waitFor()
  await page.screenshot({ path: path.join(shots, 'held-1440-empty-with-stale.png') })
  await stale.getByRole('button', { name: 'Remove old connections', exact: true }).click()
  await page.waitForFunction(() => !document.querySelector('[data-held-stale]'))
  assert.deepEqual(writes.slice(3), [['unlink', '']])
  await page.waitForFunction(() => !document.getElementById('spoolman-held'))   // no entries, no old connections: no card

  // 9. Layout for the figure and a phone, no sideways scroll; a translated page; a failed read of Spoolman's spools.
  state = fresh()
  await page.evaluate(() => localStorage.setItem('cc2-spoolman-held-seen', '99'))
  for (const width of [1440, 390]) {
    await page.setViewportSize({ width, height: 1400 })
    await open()
    await page.getByRole('heading', { name: 'Usage not sent to Spoolman (4 entries)', exact: true }).waitFor()
    await page.waitForTimeout(250)
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `no horizontal overflow at ${width}`)
    await page.locator('#spoolman-held').screenshot({ path: path.join(shots, `held-${width}-list.png`) })
  }
  language = 'de'
  await page.reload()
  await page.getByRole('heading', { name: 'Nicht an Spoolman gesendeter Verbrauch (4 Einträge)', exact: true }).waitFor()
  assert.ok((await row(4).innerText()).includes('Diese Menge wurde schon einmal gesendet, aber die Antwort von Spoolman kam nie an.'))
  assert.ok((await row(1).innerText()).includes(day('de', at - 2 * 86400)), 'German date')
  assert.equal(await row(2).getByRole('combobox', { name: 'Spool wählen: Silk Blue', exact: true }).count(), 1)
  assert.equal(await row(1).getByRole('button', { name: 'Nicht senden', exact: true }).count(), 1)
  assert.equal(await row(4).getByRole('button', { name: 'Aus Liste entfernen', exact: true }).count(), 1)
  language = 'en'

  // 10. A library bigger than the list holds says so; "Keep connections" lifts the old connections and sends nothing.
  catalog.truncated = true
  await page.reload()
  await page.getByText('Only the first 200 spools of Spoolman are listed.', { exact: true }).waitFor()
  catalog.truncated = false
  state = fresh(); state.held = []; state.stale = 3
  const before = writes.length
  await page.reload()
  await stale.getByRole('button', { name: 'Keep connections', exact: true }).waitFor()
  await stale.getByRole('button', { name: 'Keep connections', exact: true }).click()
  await page.getByText('Connections kept.').waitFor()
  await page.waitForFunction(() => !document.querySelector('[data-held-stale]'))
  assert.deepEqual(writes.slice(before), [['keep', '']], 'one request, and not a send or an unlink')

  assert.deepEqual(errors, [])
  console.log('PASS: Spoolman list of unsent usage: notice once, every reason, selection and lock, send only the chosen, Don’t send / Remove ask first and send nothing, old connections, layout and German')
} finally {
  await browser?.close()
  await server.close()
}
