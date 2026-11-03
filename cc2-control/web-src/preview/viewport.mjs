// Measure the responsive layout after resize effects/fonts/finite transitions,
// rather than racing the application's resize listener on a busy CI browser.
export async function settledViewport(page, width, height = 1100) {
  await page.setViewportSize({ width, height })
  await page.evaluate(async () => {
    await document.fonts.ready
    const frame = () => new Promise(resolve => requestAnimationFrame(resolve))
    await frame(); await frame()
    const transitions = document.getAnimations().filter(animation =>
      Number.isFinite(animation.effect?.getComputedTiming().endTime))
    await Promise.all(transitions.map(animation => animation.finished.catch(() => {})))
    await frame(); await frame()
  })
  return page.evaluate(() => ({
    viewport: innerWidth,
    width: document.documentElement.scrollWidth,
    overflow: [...document.querySelectorAll('body *')].map(element => ({
      tag: element.tagName,
      id: element.id,
      class: String(element.className).slice(0, 100),
      right: element.getBoundingClientRect().right,
      text: element.textContent?.slice(0, 80),
    })).filter(element => element.right > innerWidth + 1).slice(0, 12),
  }))
}
