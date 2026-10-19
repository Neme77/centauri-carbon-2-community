import { ls, store } from './store'

// The BIQU Panda Breath chamber heater serves its own control page on the LAN (stock firmware, or the open
// DragonBreath firmware, which is what this was tested with). CC2 Control only embeds that page; it never talks
// to the device itself. Both settings stay in this browser: the page loads directly from the browser, which must
// be on the Panda Breath's network anyway.
const ENABLED = 'cc2-pandabreath'
const HOST = 'cc2-pandabreath-host'
export const PANDABREATH_DEFAULT_HOST = 'dragonbreath.local' // DragonBreath's mDNS name; stock firmware: enter its IP

// Host name or IPv4 address, optional port; no scheme or path, so it can only ever become http://<host>/.
export const pandabreathHostValid = (host: string) =>
  /^[a-z0-9]([a-z0-9-]{0,62}[a-z0-9])?(\.[a-z0-9]([a-z0-9-]{0,62}[a-z0-9])?)*(:\d{1,5})?$/i.test(host)

const savedHost = () => {
  const host = ls.get(HOST) || ''
  return pandabreathHostValid(host) ? host : PANDABREATH_DEFAULT_HOST
}

export const pandabreath = store({ enabled: ls.get(ENABLED) === '1', host: savedHost() })

export function setPandaBreathEnabled(enabled: boolean) {
  if (enabled) ls.set(ENABLED, '1')
  else ls.del(ENABLED)
  pandabreath.set({ enabled })
}

export function setPandaBreathHost(host: string) {
  host = host.trim()
  if (!pandabreathHostValid(host)) return false
  if (host === PANDABREATH_DEFAULT_HOST) ls.del(HOST)
  else ls.set(HOST, host)
  pandabreath.set({ host })
  return true
}
