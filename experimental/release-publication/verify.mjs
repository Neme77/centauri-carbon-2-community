import {readFileSync} from 'node:fs'
import {createPublicKey,verify,createHash} from 'node:crypto'
import path from 'node:path'
const [out,metadata]=process.argv.slice(2),data=JSON.parse(readFileSync(metadata))
const trusted=JSON.parse(readFileSync(path.join(process.env.CC2_SOURCE_ROOT||'cc2-control','updater/trusted-key.json')))
if(data.public_key!==trusted.public_key)throw Error('Trust anchor mismatch')
const key=createPublicKey({key:Buffer.concat([Buffer.from('302a300506032b6570032100','hex'),Buffer.from(data.public_key,'hex')]),format:'der',type:'spki'})
const app=readFileSync(path.join(out,'CC2-Control-1.2.20-V4.4.cc2update')),n=app.readUInt32BE(8)
if(app.subarray(0,8).toString()!=='CC2UPD1\n'||!verify(null,Buffer.concat([app.subarray(0,20),app.subarray(84,84+n)]),key,app.subarray(20,84)))throw Error('Application signature refused')
const m=Object.fromEntries(app.subarray(84,84+n).toString().trim().split('\n').map(x=>x.split('=')))
if(m.version!=='1.2.20'||m.firmware!=='4.4'||Number(m.size)!==app.length-84-n||m.sha512!==createHash('sha512').update(app.subarray(84+n)).digest('hex'))throw Error('Payload refused')
const cat=readFileSync(path.join(out,'update-channel.cc2catalog'))
if(cat.length>8264||cat.subarray(0,8).toString()!=='CC2CAT1\n'||!verify(null,Buffer.concat([cat.subarray(0,8),cat.subarray(72)]),key,cat.subarray(8,72)))throw Error('Catalogue signature refused')
const index=JSON.parse(cat.subarray(72)),stable=index.releases.filter(x=>x.channel==='stable'),experimental=index.releases.filter(x=>x.channel==='canvas-uds')
if(index.schema!==1||index.expires<=Date.now()/1000||index.releases.length!==2||stable.length!==1||JSON.stringify(stable[0])!==JSON.stringify(data.stable_entry))throw Error('Stable catalogue changed')
const r=experimental[0]
if(experimental.length!==1||r.version!=='1.2.20'||r.firmware!=='4.4'||r.size!==app.length||r.url!==`https://github.com/Neme77/centauri-carbon-2-community/releases/download/${data.tag}/CC2-Control-1.2.20-V4.4.cc2update`||Object.keys(r.notes_i18n).length!==9)throw Error('Experimental entry refused')
for(const [name,sha] of Object.entries(data.files))if(createHash('sha256').update(readFileSync(path.join(out,name))).digest('hex')!==sha)throw Error('Asset checksum mismatch: '+name)
console.log('PASS signed application/catalogue, exact stable entry, explicit experimental channel, nine note languages and all asset checksums')
