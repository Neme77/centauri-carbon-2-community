import assert from 'node:assert/strict'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'

const demo = await fixture()
const browser = await chromium.launch(launchOptions)
try {
  for (const language of ['en', 'pl', 'de']) {
    for (const width of [980, 1024, 1280, 1440]) {
      for (const hasTouch of [true, false]) {
        const page = await browser.newPage({ viewport: { width, height: 1100 }, hasTouch })
        await page.addInitScript(language => localStorage.setItem('cc2-fixture-preferences',
          JSON.stringify({ language })), language)
        await page.goto(demo.url + '#control')
        await page.locator('.cc2-header-title h1:visible').waitFor()
        const boxes = await page.evaluate(() => {
          const rect = selector => {
            const element = [...document.querySelectorAll(selector)].find(el => el.getClientRects().length)
            assertVisible(element, selector)
            const { top, bottom, left, right } = element.getBoundingClientRect()
            return { top, bottom, left, right }
          }
          function assertVisible(element, selector) { if (!element) throw new Error(`No visible ${selector}`) }
          return { title: rect('.cc2-header-title h1'), header: rect('.cc2-topbar'),
            actions: rect('.cc2-header-actions'), width: document.documentElement.scrollWidth }
        })
        assert(boxes.title.top >= boxes.header.top && boxes.title.bottom <= boxes.header.bottom,
          'brand must stay inside the header on touch and desktop')
        assert(boxes.title.right <= boxes.actions.left, 'brand must not overlap status or action buttons')
        assert(boxes.width <= width, 'header must not add horizontal page overflow')
        await page.close()
      }
    }
  }
  console.log('PASS header bounds: touch/desktop, 980/1024/1280/1440px, EN/PL/DE')
} finally {
  await browser.close()
  await demo.cleanup()
}
