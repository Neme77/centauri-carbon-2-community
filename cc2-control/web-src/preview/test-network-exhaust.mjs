import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'

const labels = JSON.parse(await readFile(new URL('../public/locales/it.json', import.meta.url), 'utf8'))
const demo = await fixture(`
window.networkActions = [];
window.exhaustRebootPrepared = false;
const original = window.fetch;
const json = data => new Response(JSON.stringify(data), {headers: {'Content-Type': 'application/json'}});
window.fetch = async (input, options = {}) => {
  const path = typeof input === 'string' ? input : input.url;
  if (path === '/api/network')
    return json({available: false, fresh: true, age_ms: 0, snapshot: null});
  if (path === '/api/network/manager') {
    const manager = await (await original(input, options)).json();
    Object.assign(manager, {installed: true, ready: true, state_valid: true});
    if (window.networkActions.length)
      Object.assign(manager, {last_exit: 1, last_error: window.exhaustRebootPrepared
        ? 'ethernet_exhaust_reboot_prepared' : 'native_activation_failed',
        diagnostic: 'activation:fan_trial_requires_full_idle_reboot\\n'});
    return json(manager);
  }
  if (path === '/api/network/action') {
    window.networkActions.push(JSON.parse(options.body));
    return json({accepted: true});
  }
  return original(input, options);
};
`)
const browser = await chromium.launch(launchOptions)
try {
  for (const prepared of [false, true]) {
    const page = await browser.newPage()
    await page.goto(demo.url + '#settings')
    await page.evaluate(value => { window.exhaustRebootPrepared = value }, prepared)
    const support = page.getByRole('switch', { name: labels['network.support'] })
    await support.click()
    await page.getByRole('button', { name: labels['common.confirm'], exact: true }).click()
    const label = prepared ? 'network.ethernet_exhaust_reboot_prepared' : 'network.ethernet_exhaust_blocked'
    await page.getByRole('alert').filter({ hasText: labels[label] }).waitFor()
    assert.deepEqual(await page.evaluate(() => window.networkActions), [{ op: 'support', enabled: true }])
    assert.equal(await support.getAttribute('aria-checked'), 'false')
    await page.close()
  }
  console.log('PASS: Ethernet activation distinguishes refusal from preparation for reboot and leaves support off')
} finally {
  await browser.close()
  await demo.cleanup()
}
