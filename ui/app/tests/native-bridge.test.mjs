import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { mkdtemp, writeFile, chmod, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { nativeBridge } from '../dev/nativeBridge.mjs';

// Disposable protocol worker: no audio/device/library/owner processes.
test('bridge validates mutation envelopes, bounds FIFO, retires children and ignores old stdout', async () => {
  const folder = await mkdtemp(join(tmpdir(), 'atlas-bridge-'));
  const executable = join(folder, 'worker.mjs');
  await writeFile(executable, `#!${process.execPath}
import readline from 'node:readline';
process.on('SIGTERM', () => { setTimeout(() => { process.stdout.write('{"requestId":"retired","ok":true}\\n'); process.exit(0); }, 30); });
readline.createInterface({ input: process.stdin }).on('line', line => {
 const req = JSON.parse(line);
 if (req.method === 'hold' || req.method === 'setTransport') return;
 setTimeout(() => process.stdout.write(JSON.stringify({ requestId: req.requestId, ok: true, result: { pid: process.pid } }) + '\\n'), ['wait', 'prepareReplay', 'getCatalog'].includes(req.method) ? 150 : 0);
});
`);
  // The nested source needs actual JS backslash-n string literals.
  const { readFile } = await import('node:fs/promises');
  await writeFile(executable, (await readFile(executable, 'utf8')).replaceAll('\\\\n', '\\n'));
  await chmod(executable, 0o700);
  const previous = { binary: process.env.ATLAS_NATIVE_BINARY, audio: process.env.ATLAS_AUDIO_HOST_BINARY, library: process.env.ATLAS_LIBRARY };
  process.env.ATLAS_NATIVE_BINARY = executable; delete process.env.ATLAS_AUDIO_HOST_BINARY; process.env.ATLAS_LIBRARY = join(folder, 'unused.sqlite3');
  let handler;
  const server = http.createServer((req, res) => handler(req, res));
  const plugin = nativeBridge();
  plugin.configureServer({ httpServer: server, middlewares: { use: (_path, fn) => { handler = fn; } } });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const port = server.address().port;
  const send = (body, headers = {}) => {
    let request;
    const promise = new Promise((resolve, reject) => {
      request = http.request({ host: '127.0.0.1', port, path: '/__atlas/request', method: 'POST', headers: { host: `127.0.0.1:${port}`, origin: `http://127.0.0.1:${port}`, 'content-type': 'application/json', ...headers } }, res => {
        let text = ''; res.on('data', chunk => text += chunk); res.on('end', () => resolve({ status: res.statusCode, text }));
      });
      request.on('error', reject); request.end(typeof body === 'string' ? body : JSON.stringify(body));
    });
    return { promise, cancel: () => request.destroy() };
  };
  const envelope = (id, method = 'getCapabilities') => ({ protocolVersion: 1, requestId: id, method, args: {} });
  let workerPid;
  try {
    for (const body of [{ requestId: 'x' }, { ...envelope('x'), args: [] }, { ...envelope('x'), method: '' }, { ...envelope('x'), protocolVersion: 2 }, { ...envelope('x'), extra: 1 }]) assert.equal((await send(body).promise).status, 400);
    assert.equal((await send(envelope('x'), { origin: 'https://untrusted.example' }).promise).status, 403);
    assert.equal((await send(envelope('x'), { 'sec-fetch-site': 'cross-site' }).promise).status, 403);
    assert.equal((await send('x'.repeat(17000)).promise).status, 413);
    const first = await send(envelope('first')).promise;
    assert.equal(first.status, 200); workerPid = JSON.parse(first.text).result.pid;
    const hold = send({ ...envelope('active', 'setTransport'), args: { action: 'play', preparationId: 'test', generation: '1' } }); hold.promise.catch(() => {});
    await new Promise(resolve => setTimeout(resolve, 50));
    assert.equal((await send(envelope('active')).promise).status, 400);
    const queued = Array.from({ length: 15 }, (_, i) => send(envelope(`queued-${i}`, 'hold')));
    queued.forEach(item => item.promise.catch(() => {}));
    await new Promise(resolve => setTimeout(resolve, 50));
    assert.equal((await send(envelope('overflow')).promise).status, 429);
    hold.cancel(); queued.forEach(item => item.cancel());
    await new Promise(resolve => setTimeout(resolve, 10));
    const replacement = await send(envelope('replacement', 'wait')).promise;
    assert.equal(replacement.status, 200, replacement.text);
    const replacementPid = JSON.parse(replacement.text).result.pid;
    assert.notEqual(replacementPid, workerPid);
    workerPid = replacementPid;
    for (const method of ['prepareReplay', 'getCatalog']) {
      const preparing = send(envelope(`cancelled-${method}`, method)); preparing.promise.catch(() => {});
      await new Promise(resolve => setTimeout(resolve, 20)); preparing.cancel();
      const afterPrepare = await send(envelope(`after-${method}`)).promise;
      assert.equal(afterPrepare.status, 200);
      assert.equal(JSON.parse(afterPrepare.text).result.pid, workerPid);
    }
    const started = Date.now();
    assert.equal((await send(envelope('timeout', 'hold')).promise).status, 503);
    assert.ok(Date.now() - started >= 4500 && Date.now() - started < 8000);
  } finally {
    await new Promise(resolve => server.close(resolve));
    await new Promise(resolve => setTimeout(resolve, 100));
    if (workerPid) assert.throws(() => process.kill(workerPid, 0), /ESRCH/);
    for (const [key, value] of [['ATLAS_NATIVE_BINARY', previous.binary], ['ATLAS_AUDIO_HOST_BINARY', previous.audio], ['ATLAS_LIBRARY', previous.library]]) { if (value === undefined) delete process.env[key]; else process.env[key] = value; }
    await rm(folder, { recursive: true, force: true });
  }
});
