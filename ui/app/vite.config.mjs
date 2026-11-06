import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { resolve, dirname } from 'node:path';
import { readFileSync, readdirSync, mkdirSync, writeFileSync, copyFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { nativeBridge } from './dev/nativeBridge.mjs';

const require = createRequire(import.meta.url);
function notices(outDir) {
  return {
    name: 'atlas-offline-notices', apply: 'build',
    transformIndexHtml(html) {
      return html.replace('<head>', '<head><meta http-equiv="Content-Security-Policy" content="default-src \'self\'; script-src \'self\'; style-src \'self\' \'unsafe-inline\'; img-src \'self\' data: blob:; font-src \'self\'; connect-src \'self\'; object-src \'none\'; base-uri \'self\'">');
    },
    closeBundle() {
      const target = resolve(outDir, 'licenses'); mkdirSync(target, { recursive: true });
      const dom = dirname(require.resolve('react-dom/package.json'));
      for (const name of ['react', 'react-dom', 'scheduler', 'three']) {
        const folder = name === 'three' ? resolve(dirname(require.resolve('three')), '..') :
          dirname(require.resolve(`${name}/package.json`, { paths: [dom, process.cwd()] }));
        writeFileSync(resolve(target, `${name}.txt`), readFileSync(resolve(folder, 'LICENSE')));
      }
      for (const file of readdirSync('src/assets/fonts').filter((name) => name.startsWith('OFL-')))
        copyFileSync(resolve('src/assets/fonts', file), resolve(target, file));
      copyFileSync('docs/WORLD_ATLAS_LICENSE.txt', resolve(target, 'world-atlas.txt'));
      copyFileSync('docs/ASSET_LICENSES.md', resolve(target, 'asset-origins.md'));
    },
  };
}
export default defineConfig(({ mode }) => {
  const demo = mode === 'demo', outDir = demo ? 'dist-demo' : 'dist';
  return {
    base: './', plugins: [react(), nativeBridge(), notices(outDir)],
    server: { host: '127.0.0.1', port: 5173, strictPort: true, fs: { allow: [resolve('..')] } },
    preview: { host: '127.0.0.1', port: 4173, strictPort: true },
    build: { outDir, assetsInlineLimit: 0, sourcemap: true, target: 'es2022', chunkSizeWarningLimit: 700,
      rolldownOptions: { input: resolve(demo ? 'demo.html' : 'index.html') } },
  };
});
