import type { Key } from './i18n'
import { ls, store } from './store'

// The BIQU Panda Breath chamber heater serves its own control page on the LAN, with the stock firmware or the open
// DragonBreath firmware (which this was tested with). CC2 Control only embeds that page; it never talks to the
// device itself. All settings stay in this browser: the page loads directly from the browser, which must be on the
// Panda Breath's network anyway. The firmware switch only changes the name shown and the default (mDNS) address.
const ENABLED = 'cc2-pandabreath'
const FIRMWARE = 'cc2-pandabreath-dragonbreath'
const HOST = 'cc2-pandabreath-host'

export const pandabreathDefaultHost = (dragonbreath: boolean) =>
  dragonbreath ? 'dragonbreath.local' : 'pandabreath.local'

// Host name or IPv4 address, optional port; no scheme or path, so it can only ever become http://<host>/.
export function pandabreathHostValid(host: string) {
  if (!/^[a-z0-9]([a-z0-9-]{0,62}[a-z0-9])?(\.[a-z0-9]([a-z0-9-]{0,62}[a-z0-9])?)*(:\d{1,5})?$/i.test(host))
    return false
  if (host.split(':')[0].length > 253) return false
  const port = host.split(':')[1]
  if (port && (Number(port) < 1 || Number(port) > 65535)) return false
  try {
    const url = new URL(`http://${host}/`)
    // Never embed CC2 Control itself: that would recurse and share the parent origin.
    return typeof window === 'undefined' || url.origin !== window.location.origin
  } catch {
    return false
  }
}

const savedHost = () => {
  const host = ls.get(HOST) || ''
  return pandabreathHostValid(host) ? host : ''
}

// custom: an address the user entered, kept across firmware changes; empty means the firmware's default address.
export const pandabreath = store({
  enabled: ls.get(ENABLED) === '1',
  dragonbreath: ls.get(FIRMWARE) === '1',
  custom: savedHost(),
})

type Settings = { dragonbreath: boolean; custom: string }
export const pandabreathHost = (s: Settings) => s.custom || pandabreathDefaultHost(s.dragonbreath)
export const pandabreathName = (s: Settings): Key => (s.dragonbreath ? 'common.dragonbreath' : 'common.pandabreath')

export function setPandaBreathEnabled(enabled: boolean) {
  if (enabled) ls.set(ENABLED, '1')
  else ls.del(ENABLED)
  pandabreath.set({ enabled })
}

export function setPandaBreathFirmware(dragonbreath: boolean) {
  if (dragonbreath) ls.set(FIRMWARE, '1')
  else ls.del(FIRMWARE)
  let { custom } = pandabreath.get()
  if (custom === pandabreathDefaultHost(dragonbreath)) {
    ls.del(HOST)
    custom = ''
  }
  pandabreath.set({ dragonbreath, custom })
}

export function setPandaBreathHost(host: string) {
  host = host.trim()
  if (!pandabreathHostValid(host)) return false
  const custom = host === pandabreathDefaultHost(pandabreath.get().dragonbreath) ? '' : host
  if (custom) ls.set(HOST, custom)
  else ls.del(HOST)
  pandabreath.set({ custom })
  return true
}
