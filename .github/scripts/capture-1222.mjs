import path from 'node:path';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';
const root=path.resolve('cc2-control/web-src');
const require=createRequire(path.join(root,'package.json'));
const {createServer}=await import(pathToFileURL(require.resolve('vite')).href);
const {chromium}=require('playwright');
process.env.CC2_BACKEND='http://127.0.0.1:1';
const server=await createServer({root,configFile:path.join(root,'vite.config.ts'),mode:'demo',server:{port:0,host:'127.0.0.1'}});
let browser;
try {
 await server.listen();
 browser=await chromium.launch({headless:true});
 const page=await browser.newPage({viewport:{width:1440,height:1000}});
 const errors=[];page.on('pageerror',e=>errors.push(e.message));
 const url=server.resolvedUrls.local[0];
 for(const [route,name] of [['#dashboard','dashboard-current'],['#control','control-current'],['#settings/integrations','experiments-current']]){
  await page.goto(url+route);await page.locator('#cc2-preview-banner').waitFor();
  await page.locator('main').waitFor();await page.waitForTimeout(2200);
  await page.screenshot({path:'docs/images/cc2-control/'+name+'.png',fullPage:true});
 }
 if(errors.length)throw Error(errors.join(';'));
}finally{await browser?.close();await server.close();}
