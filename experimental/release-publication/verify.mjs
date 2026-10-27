import {readFileSync} from 'node:fs';
import {createPublicKey,verify,createHash} from 'node:crypto';
const [out,metadata]=process.argv.slice(2),d=JSON.parse(readFileSync(metadata));
if(d.public_key!=='880999fc11cc385173a4bc6724476a3375d8d1f5f828c7c2c40db2ff0250fb2d')throw Error('Wrong public anchor');
const key=createPublicKey({key:Buffer.concat([Buffer.from('302a300506032b6570032100','hex'),Buffer.from(d.public_key,'hex')]),format:'der',type:'spki'});
const app=readFileSync(out+'/CC2-Control-1.2.23-V4.4.cc2update'),n=app.readUInt32BE(8);
if(app.subarray(0,8).toString()!=='CC2UPD1\n'||!verify(null,Buffer.concat([app.subarray(0,20),app.subarray(84,84+n)]),key,app.subarray(20,84)))throw Error('Application signature refused');
const m=Object.fromEntries(app.subarray(84,84+n).toString().trim().split('\n').map(x=>x.split('=')));
if(m.printer!=='19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745'||m.daemon!=='83433b14e49d1e8aa9523282ead03618378573f4b66de1ad8f7f3c444b11aada'||m.version!=='1.2.23'||m.firmware!=='4.4'||Number(m.size)!==app.length-84-n||m.sha512!==createHash('sha512').update(app.subarray(84+n)).digest('hex'))throw Error('Payload refused');
const cat=readFileSync(out+'/update-channel.cc2catalog');
if(cat.subarray(0,8).toString()!=='CC2CAT1\n'||!verify(null,Buffer.concat([cat.subarray(0,8),cat.subarray(72)]),key,cat.subarray(8,72)))throw Error('Catalogue signature refused');
const index=JSON.parse(cat.subarray(72));if(cat.length>8264||index.releases.length!==2||index.expires<=Date.now()/1000)throw Error('Invalid catalogue');
for(const e of index.releases){if(e.url!=='https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.2.23-V4.4/CC2-Control-1.2.23-V4.4.cc2update'||e.version!=='1.2.23'||e.firmware!=='4.4'||e.size!==app.length||!['stable','canvas-uds'].includes(e.channel)||Object.keys(e.notes_i18n).length!==9)throw Error('Invalid catalogue release');}
if(index.releases[0].url!==index.releases[1].url)throw Error('Legacy route must use same package');
console.log('PASS: public-anchor app/catalog signatures, exact payload, 9 languages, stable and legacy same-package migration');
