import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { mkdtemp, writeFile, symlink, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { nativeBridge } from '../dev/nativeBridge.mjs';

test('WAV download serves only opaque app-owned regular files with same-origin boundaries', async () => {
  const folder = await mkdtemp(join(tmpdir(), 'atlas-export-route-'));
  const previous = process.env.ATLAS_EXPORT_DIRECTORY;
  process.env.ATLAS_EXPORT_DIRECTORY = folder;
  const id = 'a'.repeat(32), linkId = 'b'.repeat(32), missing = 'c'.repeat(32), wav = Buffer.from('RIFFsynthetic-WAVE-test');
  await writeFile(join(folder, `${id}.wav`), wav);
  await writeFile(join(folder, 'private.txt'), 'do not serve');
  await symlink(join(folder, 'private.txt'), join(folder, `${linkId}.wav`));
  const routes = new Map();
  const server = http.createServer((req, res) => {
    const route = [...routes.keys()].find(path => req.url.startsWith(`${path}/`));
    if (!route) { res.statusCode = 404; res.end(); return; }
    req.url = req.url.slice(route.length); routes.get(route)(req, res);
  });
  const plugin = nativeBridge(); plugin.configureServer({ httpServer: server, middlewares: { use: (path, fn) => routes.set(path, fn) } });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const port = server.address().port;
  const get = (suffix, headers = {}, method = 'GET') => new Promise((resolve, reject) => {
    const request = http.request({ host: '127.0.0.1', port, path: `/__atlas/exports/${suffix}`, method, headers: { host: `127.0.0.1:${port}`, ...headers } }, res => {
      const chunks = []; res.on('data', chunk => chunks.push(chunk)); res.on('end', () => resolve({ status: res.statusCode, headers: res.headers, body: Buffer.concat(chunks) }));
    }); request.on('error', reject); request.end();
  });
  try {
    const result = await get(id, { origin: `http://127.0.0.1:${port}`, 'sec-fetch-site': 'same-origin' });
    assert.equal(result.status, 200); assert.deepEqual(result.body, wav); assert.equal(result.headers['content-type'], 'audio/wav');
    assert.match(result.headers['content-disposition'], /^attachment;/);
    for (const suffix of [missing, linkId, '../private.txt', '%2e%2e%2fprivate.txt', `${id}?path=private.txt`, 'private.txt', id.toUpperCase()]) assert.equal((await get(suffix)).status, 404, suffix);
    assert.equal((await get(id, { origin: 'https://untrusted.example' })).status, 403);
    assert.equal((await get(id, { host: `untrusted.example:${port}` })).status, 403);
    assert.equal((await get(id, { 'sec-fetch-site': 'cross-site' })).status, 403);
    assert.equal((await get(id, {}, 'POST')).status, 405);
  } finally {
    await new Promise(resolve => server.close(resolve)); plugin.closeBundle();
    if (previous === undefined) delete process.env.ATLAS_EXPORT_DIRECTORY; else process.env.ATLAS_EXPORT_DIRECTORY = previous;
    await rm(folder, { recursive: true, force: true });
  }
});
