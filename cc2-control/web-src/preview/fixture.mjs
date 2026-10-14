import { mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
export const root = fileURLToPath(new URL('..', import.meta.url))
export async function fixture() {
  const folder = await mkdtemp(path.join(root, '.preview-tests-'))
  const locales = {}
  for (const file of await readdir(path.join(root, 'public/locales'))) {
    if (file.endsWith('.json')) locales[file.slice(0, -5)] = JSON.parse(await readFile(path.join(root, 'public/locales', file), 'utf8'))
  }
  const bootstrap = await readFile(path.join(root, 'preview/fixture-bootstrap.js'), 'utf8')
  const html = (await readFile(path.join(root, '../web/index.html'), 'utf8')).replace('<head>', '<head><script>window.__CC2_PREVIEW_LOCALES=' + JSON.stringify(locales) + ';' + bootstrap + '</script>')
  const file = path.join(folder, 'index.html')
  await writeFile(file, html)
  return { url: pathToFileURL(file).href, folder, cleanup: () => rm(folder, { recursive: true, force: true }) }
}
export const launchOptions = { headless: true, ...(process.env.CC2_BROWSER_EXECUTABLE ? { executablePath: process.env.CC2_BROWSER_EXECUTABLE } : {}) }
