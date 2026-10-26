import { createHash, createPrivateKey, createPublicKey, sign } from 'node:crypto'
import { readFile, writeFile } from 'node:fs/promises'
import { gunzipSync } from 'node:zlib'
import { fileURLToPath, pathToFileURL } from 'node:url'
import path from 'node:path'

const root = fileURLToPath(new URL('..', import.meta.url))
const MAX = 32 * 1024 * 1024

// Signing runs on the maintainer's computer. The printer does not need Node,
// OpenSSL, unzip or a whole-package buffer. Never copy the private key into
// prepared payloads, source archives, firmware images or CI artifacts.
export function signedPackage(payload, version, firmware, compatibility, privateKey, trusted) {
  if (payload.length < 1024 || payload.length > MAX || payload.length % 512)
    throw Error('Application tar size is invalid')
  if (!/^\d{1,6}\.\d{1,6}\.\d{1,6}$/.test(version) || !/^\d+\.\d+$/.test(firmware))
    throw Error('Invalid central versions')
  const publicDer = createPublicKey(privateKey).export({ type: 'spki', format: 'der' })
  const publicHex = publicDer.subarray(-32).toString('hex')
  if (trusted.algorithm !== 'Ed25519' || publicHex !== trusted.public_key)
    throw Error('Signing key does not match the installed verifier trust anchor')
  const printer = compatibility.printer_sha256, daemon = compatibility.daemon_sha256
  if (![printer, daemon].every(s => /^[a-f0-9]{64}$/.test(s))) throw Error('Invalid firmware fingerprints')
  const manifest = Buffer.from(`version=${version}\nfirmware=${firmware}\nprinter=${printer}\ndaemon=${daemon}\nsize=${payload.length}\nsha512=${createHash('sha512').update(payload).digest('hex')}\n`)
  if (manifest.length > 1024) throw Error('Manifest exceeds verifier limit')
  const header = Buffer.alloc(20)
  header.write('CC2UPD1\n', 0, 'ascii')
  header.writeUInt32BE(manifest.length, 8)
  header.writeBigUInt64BE(BigInt(payload.length), 12)
  const signature = sign(null, Buffer.concat([header, manifest]), privateKey)
  if (signature.length !== 64) throw Error('Ed25519 signature required')
  return Buffer.concat([header, signature, manifest, payload])
}

async function main(args) {
  const values = {}
  for (let i = 0; i < args.length; i += 2) {
    if (!['--payload', '--key', '--output'].includes(args[i]) || !args[i + 1]) throw Error('Usage: node installer/sign-update.mjs --payload cc2-control-payload.tar.gz --key PRIVATE-KEY.pem --output UPDATE.cc2update')
    values[args[i]] = args[i + 1]
  }
  if (!values['--payload'] || !values['--key'] || !values['--output']) throw Error('Payload, private key and output are required')
  if (!values['--output'].endsWith('.cc2update')) throw Error('Use the .cc2update file extension')
  const version = (await readFile(path.join(root, 'VERSION'), 'utf8')).trim()
  const firmware = (await readFile(path.join(root, 'FIRMWARE_VERSION'), 'utf8')).trim()
  const compatibility = JSON.parse(await readFile(path.join(root, 'installer/compatibility.json'), 'utf8'))
  const trusted = JSON.parse(await readFile(path.join(root, 'updater/trusted-key.json'), 'utf8'))
  const key = createPrivateKey(await readFile(values['--key']))
  const payload = gunzipSync(await readFile(values['--payload']), { maxOutputLength: MAX })
  const result = signedPackage(payload, version, firmware, compatibility, key, trusted)
  await writeFile(values['--output'], result, { flag: 'wx' })
  const digest = createHash('sha256').update(result).digest('hex')
  await writeFile(values['--output'] + '.sha256', `${digest}  ${path.basename(values['--output'])}\n`, { flag: 'wx' })
  console.log('Signed application update:', values['--output'])
  console.log('SHA256:', digest)
  console.log('Public key ID:', trusted.key_id)
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href)
  main(process.argv.slice(2)).catch(e => { console.error(e.message); process.exitCode = 1 })
