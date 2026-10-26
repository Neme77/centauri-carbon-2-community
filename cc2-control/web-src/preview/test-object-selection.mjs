import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'
const demo = await fixture()
const captured = JSON.parse(await readFile(new URL('./fixtures/elegoo-object-coordinates.json',import.meta.url)))
const browser = await chromium.launch(launchOptions)
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  await page.addInitScript(() => localStorage.setItem('cc2-fixture-preferences', '{"language":"en"}'))
  await page.goto(demo.url + '#job')
  await page.evaluate(() => {
    const state = window.__CC2_PREVIEW
    state.status = 2
    state.objectRows = [
      { name: 'Buddha.stl_id_3_copy_0', center: [150, 150] },
      { name: 'Buddha.stl_id_0_copy_0', center: [150, 50] },
      { name: 'Buddha.stl_id_2_copy_0', center: [50, 150] },
      { name: 'Buddha.stl_id_1_copy_0', center: [50, 50] },
    ]
    state.excluded = []
    const original = window.fetch
    window.fetch = (url, opts = {}) => {
      if (String(url).includes('/api/printer')) return original(url, opts).then(async response => {
        const data = await response.json()
        data.print.filename = 'copies.gcode'
        return new Response(JSON.stringify(data), {headers:{'Content-Type':'application/json'}})
      })
      if (String(url).includes('/api/exclude-objects')) return Promise.resolve(new Response(JSON.stringify({result:{status:{exclude_object:{objects:state.objectRows, current_object:state.objectRows.at(-1)?.name || null, excluded_objects:state.excluded}}}}), {headers:{'Content-Type':'application/json'}}))
      if (String(url).includes('/api/control') && String(opts.body).startsWith('object:exclude:')) {
        state.commands.push(opts.body)
        state.excluded.push(String(opts.body).slice('object:exclude:'.length))
        return Promise.resolve(new Response('{"accepted":true}', {headers:{'Content-Type':'application/json'}}))
      }
      return original(url, opts)
    }
  })
  const buttons = page.locator('[data-object-id]')
  await buttons.first().waitFor()
  assert.deepEqual(await buttons.evaluateAll(nodes => nodes.map(n => n.dataset.objectId)), [
    'Buddha.stl_id_1_copy_0','Buddha.stl_id_0_copy_0','Buddha.stl_id_2_copy_0','Buddha.stl_id_3_copy_0',
  ])
  for (let n=1;n<=4;n++) assert.match(await buttons.nth(n-1).innerText(), new RegExp(`Buddha-${n}`))
  await buttons.nth(2).click()
  await page.evaluate(() => window.__CC2_PREVIEW.objectRows.reverse())
  await page.waitForTimeout(2300)
  await page.getByRole('button', { name:'Exclude Selected Object', exact:true }).click()
  const dialog = page.getByRole('dialog')
  await dialog.waitFor()
  assert.match(await dialog.innerText(), /Buddha-3/)
  await dialog.getByRole('button', {name:'Confirm', exact:true}).click()
  await page.waitForFunction(() => window.__CC2_PREVIEW.commands.length === 1)
  assert.deepEqual(await page.evaluate(() => window.__CC2_PREVIEW.commands), ['object:exclude:Buddha.stl_id_2_copy_0'])
  await page.waitForFunction(() => document.querySelectorAll('[data-object-id]')[2].disabled)
  await page.evaluate(rows=>{window.__CC2_PREVIEW.objectRows=rows;window.__CC2_PREVIEW.excluded=[]},captured.result.status.exclude_object.objects)
  await page.waitForFunction(()=>document.querySelector('[data-object-id]')?.getAttribute('data-object-id')==='GPUSUPPORT_SCREW.STL_ID_1_COPY_0')
  assert.deepEqual(await buttons.evaluateAll(nodes=>nodes.map(n=>n.dataset.objectId)),['GPUSUPPORT_SCREW.STL_ID_1_COPY_0','GPUSUPPORT_STOP.STL_ID_2_COPY_0','GPUSUPPORT_BASE.STL_ID_0_COPY_0'])
  await buttons.first().click()
  assert.match(await page.locator('[data-bed-selection]').innerText(), /X 47\.4 \/ Y 115\.2 mm/)
  await buttons.nth(1).click()
  await page.getByRole('button',{name:'Exclude Selected Object',exact:true}).click()
  await dialog.getByRole('button',{name:'Confirm',exact:true}).click()
  await page.waitForFunction(()=>window.__CC2_PREVIEW.commands.length===2)
  assert.equal((await page.evaluate(()=>window.__CC2_PREVIEW.commands))[1],'object:exclude:GPUSUPPORT_STOP.STL_ID_2_COPY_0')
  const rawId = 'pièce_测试_"Buddha".stl_id_0_copy_0'
  await page.evaluate(id => { window.__CC2_PREVIEW.objectRows = [{name:id}]; window.__CC2_PREVIEW.excluded = [] }, rawId)
  await page.waitForFunction(id => document.querySelector('[data-object-id]')?.getAttribute('data-object-id') === id, rawId)
  assert.match(await buttons.first().innerText(), /pièce 测试 "Buddha"/)
  await buttons.first().click()
  await page.getByText('Position unavailable', {exact:true}).waitFor()
  await page.getByRole('button', { name:'Exclude Selected Object', exact:true }).click()
  await dialog.getByRole('button', {name:'Confirm', exact:true}).click()
  await page.waitForFunction(() => window.__CC2_PREVIEW.commands.length === 3)
  assert.equal((await page.evaluate(() => window.__CC2_PREVIEW.commands))[2], `object:exclude:${rawId}`)
  for (const width of [390,768,1440]) {
    await page.setViewportSize({width,height:1000})
    await page.waitForTimeout(250)
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth))
  }
  assert.deepEqual(errors, [])
  await page.evaluate(()=>{window.__CC2_PREVIEW.status=1})
  await page.getByText('No live object data',{exact:true}).waitFor()
  assert.equal(await buttons.count(),0,'idle clears previous objects and selection')
  console.log('PASS: real object selection, spatial labels, polling reorder and exact original exclusion command; responsive layout')
} finally { await browser.close(); await demo.cleanup() }
