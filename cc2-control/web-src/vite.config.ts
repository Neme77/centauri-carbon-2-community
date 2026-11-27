import { fileURLToPath } from 'node:url'
import { readdirSync, readFileSync, writeFileSync } from 'node:fs'
import { defineConfig, type UserConfig, type Plugin } from 'vite'
import preact from '@preact/preset-vite'
import tailwindcss from '@tailwindcss/vite'
import { viteSingleFile } from 'vite-plugin-singlefile'

/* The localisation files are served whole to a memory-constrained device, so
 * the shipped copy is compact JSON. The source files stay readable and remain
 * the single source of truth; test_web_sync_static.py compares the parsed
 * content, not the formatting. */
const compactLocales = (): Plugin => ({
  name: 'cc2-compact-locales',
  closeBundle() {
    const source = fileURLToPath(new URL('./public/locales', import.meta.url))
    const target = fileURLToPath(new URL('../web/locales', import.meta.url))
    for (const name of readdirSync(source).filter(name => name.endsWith('.json'))) {
      const value = JSON.parse(readFileSync(`${source}/${name}`, 'utf8'))
      writeFileSync(`${target}/${name}`, JSON.stringify(value))
    }
  },
})

// `npm run build` writes the single self-contained ../web/index.html and copies public/ (the locales) next to it in ../web/locales.
// `npm run dev` proxies the API to a running CC2 Control: CC2_BACKEND=http://host:8081 npm run dev
const backend = process.env.CC2_BACKEND || 'http://localhost:8081'

export default defineConfig(async ({ mode, command }): Promise<UserConfig> => {
  if (mode === 'demo' && command !== 'serve')
    throw new Error('Demo is development-only; use npm run build for firmware')
  const demo = mode === 'demo'
  const preview = demo ? (await import('./preview/plugin.ts')).previewPlugin() : null
  return {
    plugins: [...(preview ? [preview] : []), preact(), tailwindcss(), viteSingleFile(), compactLocales()],
    resolve: { alias: { '@': fileURLToPath(new URL('./src', import.meta.url)) } },
    server: demo ? { host: '127.0.0.1' } : { proxy: { '/api': backend, '/i18n': backend } },
    build: { outDir: '../web', emptyOutDir: false, target: 'esnext', modulePreload: false, cssMinify: 'lightningcss' },
  }
})
