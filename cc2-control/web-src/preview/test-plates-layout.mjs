// The Bed levelling page after the reordering: four tabs (the old #bed/cell link lands in the Mesh tab), the load cell as a folded
// block in the Mesh card, the note of the loaded mesh in the card head, the nozzle expansion in two columns, the plate and nozzle
// library with its "…" menus and bounded lists (also with many entries), and no sideways scrolling at 390 px and 150 %.
// Many entries: the test serves its own library with 9 plates, one with 14 measurements, and 9 nozzles (see `library` below).
import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' }, logLevel: 'error' })
let browser
try {
  await server.listen()
  browser = await chromium.launch({
    headless: true,
    ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox'] } : process.env.CC2_BROWSER_EXECUTABLE ? { executablePath: process.env.CC2_BROWSER_EXECUTABLE } : {}),
  })
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  const errors = []
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  page.on('pageerror', e => errors.push(e.message))
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  const tabs = () => page.locator('[role="tab"]')

  // The test's own plate library: the preview's library plus more plates, measurements and nozzles. It answers the
  // two actions of the deleting tests itself (one delete per nozzle, all measurements of a plate), like the printer.
  const base = await page.evaluate(async () => (await fetch('/api/plates')).json())
  const hex = (n, width = 16) => n.toString(16).padStart(width, '0')
  const library = structuredClone(base)
  const template = base.plates[0].measures[0]
  const measure = (id, temp, k) => ({ ...structuredClone(template), id, temp, nozzle: k % 2 ? base.nozzles[0].id : '', measured: 1790000000 + k * 1000, slot: false })
  const names = ['Smooth PEI zweite Platte mit sehr langem Namen', 'Glatte Federstahlplatte', 'Struktur PEI Gold', 'Garolite G10', 'PC-Platte', 'Kalte Platte 2']
  names.forEach((name, i) => {
    const temps = [40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100, 105, 110].slice(0, 4 + i * 2)
    const measures = temps.map((temp, k) => measure(hex(0xf0000000 + i * 100 + k), temp, k))
    library.plates.push({ ...structuredClone(base.plates[0]), id: hex(0xe0000000 + i), name, side: i % 2 ? 'B' : 'A', z_offset: -0.01 * i, measure: measures[0].id, in_printer: false, measures })
  })
  const mounted = library.plates.find(p => p.id === library.current)
  for (const [k, temp] of [40, 50, 70, 90, 110, 45, 55, 65, 75, 85].entries()) mounted.measures.push(measure(hex(0xaa000000 + temp), temp, k))
  for (let k = 0; k < 8; k++) library.nozzles.push({ id: hex(0xd1000000 + k), name: `${(0.2 + k * 0.2).toFixed(1)} ${k % 2 ? 'hardened steel' : 'brass'} Ersatz ${k + 1}`, diameter: 0.2 + k * 0.2, z_offset: k * 0.005 })
  const idOf = route => route.request().postData()?.trim()
  await page.route('**/api/plates', route => (route.request().method() === 'GET' ? route.fulfill({ json: library }) : route.fallback()))
  await page.route('**/api/plates/nozzle/delete', route => {
    library.nozzles = library.nozzles.filter(n => n.id !== idOf(route))
    return route.fulfill({ json: { deleted: true } })
  })
  await page.route('**/api/plates/measures/clear', route => {
    const plate = library.plates.find(p => p.id === idOf(route))
    if (!plate) return route.fulfill({ status: 404, json: { ok: false, error: 'Unknown plate' } })
    if (library.current === plate.id) return route.fulfill({ status: 409, json: { ok: false, error: 'Remove the plate from the printer first, then delete all its measurements' } })
    const cleared = plate.measures.length
    plate.measures = []
    plate.measure = ''
    return route.fulfill({ json: { cleared } })
  })

  // Four tabs; the old link to the load cell lands in the Mesh tab, which holds the folded load cell block.
  await page.goto(`${origin}/#bed/cell`)
  await tabs().first().waitFor()
  assert.equal(await tabs().count(), 4)
  assert.deepEqual(await tabs().allInnerTexts(), ['Mesh & Saved Profiles', 'Bed alignment', 'Build plates & nozzles', 'Nozzle expansion'])
  assert.equal(await tabs().first().getAttribute('aria-selected'), 'true', '#bed/cell leads to the Mesh tab')
  const cell = page.locator('[data-bed-cell]')
  await cell.waitFor()
  assert.equal(await cell.evaluate(e => e.open), false, 'the load cell block is folded')
  assert.match(await cell.locator('summary').innerText(), /^Load cell\s+read the sensor, diagnosis, tare, guided calibration$/)
  assert.equal(await page.getByRole('heading', { name: 'Load cell' }).count(), 0, 'no card head of its own')
  await cell.locator('summary').click()
  assert.equal(await cell.evaluate(e => e.open), true)
  await cell.locator('summary').click()
  // The note of the loaded mesh sits in the card head, without a full stop; the bottom has only the buttons.
  const note = page.locator('[data-mesh-note]')
  await note.waitFor()
  assert.match(await note.innerText(), /^(Active mesh loaded|Saved mesh loaded|Waiting for the printer mesh)/)
  assert.equal(/\.$/.test((await note.innerText()).trim()), false, 'no full stop')
  const head = await page.locator('[data-mesh-note]').evaluate(e => e.parentElement.getBoundingClientRect())
  const noteBox = await note.boundingBox()
  assert.ok(noteBox.y >= head.y && noteBox.y + noteBox.height <= head.y + head.height + 2, 'the note is in the head row')

  // A saved measurement that is older than the newest screw measurement carries a warning; a newer one does not, and
  // neither does the printer's active mesh (it has no time).
  {
    const newest = library.plates[0].measures.reduce((m, x) => (x.measured > m.measured ? x : m))
    const oldest = library.plates[0].measures.reduce((m, x) => (x.measured < m.measured ? x : m))
    const between = Math.floor((newest.measured + oldest.measured) / 2)
    await page.route('**/api/screws/history', route =>
      route.fulfill({ json: { sessions: [{ kind: 'quick', points: 'screws', started: between - 60, ended: between, measurements: [{ time: between, kind: 'quick', corner: null, twice: false, z: { FL: 0, FR: 0, RR: 0, RL: 0 } }] }] } })
    )
    await page.reload()
    await page.locator('[data-mesh-note]').waitFor()
    const pick = page.locator('label:has-text("Mesh profile") select')
    assert.equal(await page.locator('[data-mesh-before-screws]').count(), 0, 'the active mesh carries no warning')
    if (newest.measured !== oldest.measured) {
      await pick.selectOption(`measure:${oldest.id}`)
      await page.locator('[data-mesh-before-screws]').getByText('This mesh is older than the last screw measurement', { exact: false }).waitFor({ timeout: 8000 })
      await pick.selectOption(`measure:${newest.id}`)
      await page.locator('[data-mesh-before-screws]').waitFor({ state: 'detached', timeout: 8000 })
    }
    await pick.selectOption('active')
  }

  // Nozzle expansion: one card, two columns from the large layout on, a divider between them; stacked on a phone.
  await tabs().nth(3).click()
  const run = page.locator('[data-nozzle-run]')
  await run.waitFor()
  assert.equal(await page.locator('[data-nozzle-expansion]').count(), 1, 'one card')
  const left = await page.locator('[data-nozzle-expansion] > div > div').first().boundingBox()
  const right = await run.boundingBox()
  assert.ok(right.x >= left.x + left.width - 2 && Math.abs(right.y - left.y) < 8, 'two columns side by side')
  assert.equal(await run.evaluate(e => getComputedStyle(e).borderLeftWidth), '1px', 'a vertical divider')

  // Plates & nozzles: title, two columns on top, the nozzles with at most three rows visible and a scroll hint.
  await tabs().nth(2).click()
  await page.getByRole('heading', { name: 'Build plate and nozzle library' }).waitFor()
  const top = page.locator('[data-plates-top]')
  const now = await top.locator('[data-plates-now]').boundingBox()
  const nozzles = page.locator('[data-plates-nozzles]')
  const nb = await nozzles.boundingBox()
  assert.ok(nb.y > now.y && Math.abs(nb.x - now.x) < 8, 'the nozzles are under "In the printer now"')
  const selector = await top.locator('[data-mesh-selector]').boundingBox()
  assert.ok(selector.x > now.x + now.width * 0.4, 'the library mesh and the switch are in the right column')
  assert.equal(await page.locator('[data-nozzle-form]').count(), 0, 'no nozzle form until "+ New nozzle"')
  const list = page.locator('[data-nozzle-list]')
  assert.ok((await list.evaluate(e => e.scrollHeight - e.clientHeight)) > 0, 'with many nozzles the list scrolls')
  assert.ok((await list.evaluate(e => e.clientHeight)) <= 160, 'about three rows are visible')
  await nozzles.locator('[data-more-below]').getByText(/entries · scroll the list/).waitFor()
  assert.equal(await page.locator('[data-plates-how]').evaluate(e => e.open), false, '"How the plate library works" is folded')
  assert.equal(await page.locator('[data-plates-add]').innerText(), '+ New plate')
  assert.equal(await page.getByRole('heading', { name: 'New plate profile' }).count(), 0, 'no plate form until "+ New plate"')

  // A plate with many measurements: its list is bounded (about four rows) and scrolls; cards keep their own height.
  const many = page.locator('[data-plate]').filter({ has: page.locator('[data-measure-list]') }).filter({ hasText: 'Kalte Platte 2' })
  await many.scrollIntoViewIfNeeded()
  const mlist = many.locator('[data-measure-list]')
  assert.ok((await mlist.evaluate(e => e.scrollHeight - e.clientHeight)) > 0, 'many measurements: the list scrolls')
  assert.ok((await mlist.evaluate(e => e.clientHeight)) <= 210, 'about two and a half rows are visible (the third almost whole, the fourth not)')
  await many.locator('[data-more-below]').getByText(/entries · scroll the list/).waitFor()
  const heights = await page.locator('[data-plate]').evaluateAll(es => es.map(e => Math.round(e.getBoundingClientRect().height)))
  assert.equal(new Set(heights).size, 1, `all plate cards are equally high (${[...new Set(heights)].join(' / ')})`)
  const foot = await page.locator('[data-plate]').evaluateAll(es => es.map(e => Math.round(e.lastElementChild.getBoundingClientRect().bottom - e.getBoundingClientRect().top)))
  assert.equal(new Set(foot).size, 1, 'the button row sits at the bottom of every card')

  // The "…" menu of a row: fixed on the screen (not cut off by the scrolling list), closes on a click outside and on scrolling.
  const firstRow = many.locator('[data-measure]').first()
  await firstRow.locator('[data-more] summary').click()
  const panel = firstRow.locator('[data-more] [role="menu"]')
  await panel.waitFor()
  assert.equal(await panel.evaluate(e => getComputedStyle(e).position), 'fixed')
  const pb = await panel.boundingBox()
  assert.ok(pb.x >= 0 && pb.y >= 0 && pb.x + pb.width <= 1440 && pb.y + pb.height <= 1000, 'the menu lies inside the screen')
  for (const name of ['Edit measurement']) await panel.getByRole('button', { name, exact: true }).waitFor()
  await page.mouse.click(5, 5)
  await panel.waitFor({ state: 'detached' })
  await firstRow.locator('[data-more] summary').click()
  await panel.waitFor()
  await mlist.evaluate(e => e.scrollBy(0, 40))
  await panel.waitFor({ state: 'detached' })
  // Deleting still asks first (the safeguard is unchanged): open the menu of a row that can be deleted and cancel.
  await many.locator('[data-measure]').nth(1).locator('[data-more] summary').click()
  await many.locator('[data-measure]').nth(1).getByRole('button', { name: 'Delete', exact: true }).click()
  await page.getByRole('dialog').getByRole('button', { name: 'Cancel', exact: true }).click()
  // The menu near the bottom edge opens upwards (it must not leave the screen).
  await page.evaluate(() => window.scrollTo(0, document.body.scrollHeight))
  const lastCard = page.locator('[data-plate]').last()
  await lastCard.locator('[data-more] summary').last().click()
  const lp = lastCard.locator('[data-more] [role="menu"]').last()
  await lp.waitFor()
  const lb = await lp.boundingBox()
  assert.ok(lb.y >= 0 && lb.y + lb.height <= 1000, 'the menu stays on the screen at the bottom edge')
  await page.mouse.click(5, 5)

  // Delete all nozzles: one "…" menu next to "+ New nozzle" (only with more than one nozzle), one confirmation, then one request per nozzle.
  const posts = []
  page.on('request', r => {
    if (r.method() === 'POST' && new URL(r.url()).pathname.startsWith('/api/plates/')) posts.push(new URL(r.url()).pathname)
  })
  await page.evaluate(() => window.scrollTo(0, 0))
  const all = nozzles.locator('[data-nozzle-delete-all]')
  await nozzles.locator('[data-more] summary').first().click()
  await all.waitFor()
  await all.click()
  const dialog = page.getByRole('dialog')
  await dialog.getByText(/^Delete all \d+ nozzles\? Measurements made with them keep their meshes\.$/).waitFor()
  await dialog.getByRole('button', { name: 'Cancel', exact: true }).click()
  assert.deepEqual(posts, [], 'cancelling deletes nothing')
  const nozzleCount = await page.locator('[data-nozzle]').count()
  await nozzles.locator('[data-more] summary').first().click()
  await all.click()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.getByText('All nozzles deleted.', { exact: true }).waitFor()
  assert.equal(posts.filter(x => x === '/api/plates/nozzle/delete').length, nozzleCount, 'one delete request per nozzle (the checks of a single delete apply to each)')
  assert.equal(await page.locator('[data-nozzle]').count(), 0)
  assert.equal(await nozzles.locator('[data-nozzle-delete-all]').count(), 0, 'no menu for the delete-all action without nozzles')

  // Delete all measurements of a plate: in the card menu, refused (greyed out) for the mounted plate, one confirmation, a new request.
  posts.length = 0
  const mountedCard = page.locator('[data-plate]').filter({ hasText: 'Smooth PEI' }).first()
  await mountedCard.locator('[data-more] summary').last().click()
  const clearMounted = mountedCard.locator('[data-clear-measures]')
  assert.ok(await clearMounted.isDisabled(), 'the mounted plate keeps its measurements: the action is greyed out')
  assert.equal(await clearMounted.getAttribute('title'), 'Remove the plate first, then delete all its measurements')
  await mountedCard.locator('[data-more] summary').last().click()
  const slate = page.locator('[data-plate]').filter({ hasText: 'Kalte Platte 2' })
  const n = await slate.locator('[data-measure]').count()
  await slate.locator('[data-more] summary').last().click()
  await slate.locator('[data-clear-measures]').click()
  await dialog.getByText(`Delete all ${n} measurements of “Kalte Platte 2”? The plate stays and must be calibrated again afterwards.`, { exact: true }).waitFor()
  await dialog.getByRole('button', { name: 'Cancel', exact: true }).click()
  assert.deepEqual(posts, [], 'cancelling deletes nothing')
  await slate.locator('[data-more] summary').last().click()
  await slate.locator('[data-clear-measures]').click()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.getByText('All measurements deleted.', { exact: true }).waitFor()
  assert.deepEqual(posts.filter(x => x.includes('measure')), ['/api/plates/measures/clear'], 'one request, the plate id as body')
  await slate.locator('[data-measure]').first().waitFor({ state: 'detached' })
  await slate.getByText(/Not calibrated/i).first().waitFor()
  assert.ok(await slate.getByRole('button', { name: 'Mount', exact: true }).isDisabled(), 'an empty plate cannot be mounted')
  assert.equal(await page.locator('[data-plate]').count(), 9, 'the plate itself stays')

  // The heads look alike on every tab: the icon chip and the title sit at the same place.
  const heads = {}
  for (const hash of ['mesh', 'align', 'plates', 'nozzle']) {
    await page.goto(`${origin}/#bed/${hash}`)
    await page.locator('.cc2-card-head').first().waitFor()
    heads[hash] = await page.evaluate(() => {
      const h = document.querySelector('main .cc2-card-head'), chip = h.querySelector('.cc2-card-chip').getBoundingClientRect(), title = h.querySelector('h2').getBoundingClientRect(), icon = h.querySelector('.cc2-card-chip svg').getBoundingClientRect()
      return [Math.round(chip.x), Math.round(chip.y - h.getBoundingClientRect().y), Math.round(icon.width), Math.round(title.x)].join(',')
    })
  }
  assert.equal(new Set(Object.values(heads)).size, 1, `icon and title sit at the same place on all tabs ${JSON.stringify(heads)}`)
  await page.goto(`${origin}/#bed/nozzle`)
  await page.getByText('How the nozzle grows as it heats up', { exact: true }).waitFor()
  await page.getByText('Measure the expansion at several temperatures', { exact: true }).waitFor()

  // 390 px and 150 % text: no sideways scrolling on any of the four tabs.
  await page.setViewportSize({ width: 390, height: 844 })
  for (const size of ['', '150%']) {
    await page.evaluate(s => (document.documentElement.style.fontSize = s), size)
    for (const [i, hash] of ['mesh', 'align', 'plates', 'nozzle'].entries()) {
      await page.goto(`${origin}/#bed/${hash}`)
      await page.evaluate(s => (document.documentElement.style.fontSize = s), size)
      await page.waitForTimeout(250)
      const wide = await page.evaluate(() => (document.documentElement.scrollWidth <= innerWidth + 1 ? [] : [...document.querySelectorAll('main *')].filter(e => e.getBoundingClientRect().right > innerWidth + 1 && !e.closest('[role=tablist]')).slice(0, 5).map(e => `${e.tagName}.${String(e.className).slice(0, 40)} ${Math.round(e.getBoundingClientRect().right)}`)))
      assert.deepEqual(wide, [], `no sideways scrolling on tab ${i + 1} (${hash}) at ${size || '100%'} and 390 px`)
    }
  }
  assert.deepEqual(errors, [])
  console.log('PASS bed levelling layout: four tabs, load cell block, mesh note, nozzle columns, library lists and ⋯ menus with many entries, 390 px and 150 %')
} finally {
  await browser?.close()
  await server.close()
}
