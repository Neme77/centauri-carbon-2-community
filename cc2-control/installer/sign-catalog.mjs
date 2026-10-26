import { createPrivateKey, createPublicKey, sign } from 'node:crypto'
import { readFile, writeFile } from 'node:fs/promises'

const [input, keyPath, output] = process.argv.slice(2)
if (!input || !keyPath || !output) throw Error('Usage: node sign-catalog.mjs INDEX.json PRIVATE-KEY.pem INDEX.cc2catalog')
const index = JSON.parse(await readFile(input, 'utf8'))
const firmware = (await readFile(new URL('../FIRMWARE_VERSION', import.meta.url), 'utf8')).trim()
if (index.schema !== 1 || !Array.isArray(index.releases) || index.releases.length > 8 ||
    !Number.isSafeInteger(index.expires) || index.expires <= Date.now() / 1000)
  throw Error('Invalid or expired update index')
for (const release of index.releases) {
  if (!/^\d{1,6}\.\d{1,6}\.\d{1,6}$/.test(release.version) || release.firmware !== firmware ||
      !['stable', 'canvas-uds'].includes(release.channel) ||
      !/^https:\/\/github\.com\/Neme77\/centauri-carbon-2-community\/releases\/download\/[A-Za-z0-9._-]+\/[A-Za-z0-9._-]+\.cc2update$/.test(release.url) ||
      !Number.isSafeInteger(release.size) || release.size < 1108 || release.size > 33555540 ||
      typeof release.notes !== 'string' || release.notes.length > 2048)
    throw Error('Invalid release: only firmware-qualified application packages are accepted')
}
const payload = Buffer.from(JSON.stringify(index))
if (payload.length > 8192) throw Error('Index exceeds the printer limit')
const trusted = JSON.parse(await readFile(new URL('../updater/trusted-key.json', import.meta.url), 'utf8'))
const key = createPrivateKey(await readFile(keyPath))
const publicHex = createPublicKey(key).export({ type: 'spki', format: 'der' }).subarray(-32).toString('hex')
if (publicHex !== trusted.public_key) throw Error('Wrong release signing key')
const magic = Buffer.from('CC2CAT1\n')
await writeFile(output, Buffer.concat([magic, sign(null, Buffer.concat([magic, payload]), key), payload]), { flag: 'wx' })
console.log('Signed update index:', output)
