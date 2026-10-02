import { fileURLToPath } from 'node:url'
import { defineConfig, type UserConfig } from 'vite'
import preact from '@preact/preset-vite'
import tailwindcss from '@tailwindcss/vite'
import { viteSingleFile } from 'vite-plugin-singlefile'

// `npm run build` writes the single self-contained ../web/index.html and copies public/ (the locales) next to it in ../web/locales.
// `npm run dev` proxies the API to a running CC2 Control: CC2_BACKEND=http://host:8081 npm run dev
const backend = process.env.CC2_BACKEND || 'http://localhost:8081'

export default defineConfig(async ({ mode, command }): Promise<UserConfig> => {
  if (mode === 'demo' && command !== 'serve')
    throw new Error('Demo is development-only; use npm run build for firmware')
  const demo = mode === 'demo'
  const preview = demo ? (await import('./preview/plugin')).previewPlugin() : null
  return {
    plugins: [...(preview ? [preview] : []), preact(), tailwindcss(), viteSingleFile()],
    resolve: { alias: { '@': fileURLToPath(new URL('./src', import.meta.url)) } },
    server: demo ? { host: '127.0.0.1' } : { proxy: { '/api': backend, '/i18n': backend } },
    build: { outDir: '../web', emptyOutDir: false, target: 'esnext', modulePreload: false, cssMinify: 'lightningcss' },
  }
})
