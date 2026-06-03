import { defineConfig } from 'vite'
import preact from '@preact/preset-vite'
import { viteSingleFile } from 'vite-plugin-singlefile'
import { resolve } from 'path'

export default defineConfig({
  plugins: [preact(), viteSingleFile()],
  build: {
    outDir: resolve(__dirname, '../src/web'),
    emptyOutDir: false,
    rollupOptions: { input: resolve(__dirname, 'client.html') },
  },
})
