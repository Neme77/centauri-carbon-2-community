import { fileURLToPath } from 'node:url'
import { defineConfig } from 'vite'
import preact from '@preact/preset-vite'
import tailwindcss from '@tailwindcss/vite'
import { viteSingleFile } from 'vite-plugin-singlefile'

// `npm run build` writes the single self-contained ../web/index.html (locales/ stays untouched).
// `npm run dev` proxies the API to a running CC2 Control: CC2_BACKEND=http://host:8081 npm run dev
const backend = process.env.CC2_BACKEND || 'http://localhost:8081'

export default defineConfig({
  plugins: [preact(), tailwindcss(), viteSingleFile()],
  resolve: { alias: { '@': fileURLToPath(new URL('./src', import.meta.url)) } },
  server: { proxy: { '/api': backend, '/i18n': backend } },
  build: { outDir: '../web', emptyOutDir: false, target: 'esnext', modulePreload: false, cssMinify: 'lightningcss' },
})
