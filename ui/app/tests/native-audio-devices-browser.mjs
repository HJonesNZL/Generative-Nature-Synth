// Real C++ UiHost/PlaybackRuntime device protocol through an isolated Vite bridge.
// Uses a disposable synthetic library and fake output driver; no physical audio.
import assert from 'node:assert/strict';
import { spawn, execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdtemp, mkdir, readFile, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, resolve, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const app = resolve(dirname(fileURLToPath(import.meta.url)), '..'), root = resolve(app, '../..');
const folder = await mkdtemp(join(tmpdir(), 'atlas-native-device-ui-'));
const binary = process.env.ATLAS_STUDIO_FIXTURE_BINARY || resolve(root, 'build/native/atlas-studio-fixture-host');
const port = Number(process.env.ATLAS_NATIVE_DEVICES_TEST_PORT || 5189), origin = `http://127.0.0.1:${port}`;
const library = join(folder, 'synthetic.sqlite3');
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
let browser, server, closed, page;
const errors = [], rejected = [], replies = [], mutations = [];
async function stop() {
  if (!server) return;
  server.stdin.destroy(); server.kill('SIGTERM');
  const stopped = await Promise.race([closed.then(() => true), new Promise(done => setTimeout(() => done(false), 2000))]);
  if (!stopped) { server.kill('SIGKILL'); await closed; }
  server = null;
}
async function post(method, args) {
  const response = await fetch(`${origin}/__atlas/request`, { method: 'POST', headers: { Origin: origin, 'Content-Type': 'application/json' }, body: JSON.stringify({ protocolVersion: 1, requestId: `device-test-${Date.now()}`, method, args }), signal: AbortSignal.timeout(10000) });
  assert.equal(response.status, 200); return await response.json();
}
try {
  execFileSync('python3', ['-B', 'tests/ui_fixture.py', library], { cwd: root, stdio: 'pipe' });
  server = spawn(process.execPath, ['node_modules/vite/bin/vite.js', '--port', String(port)], { cwd: app, env: { ...process.env, ATLAS_AUDIO_HOST_BINARY: '', ATLAS_NATIVE_BINARY: binary, ATLAS_LIBRARY: library, ATLAS_VOICE_LIBRARY: join(folder, 'voices.sqlite3'), ATLAS_SOUNDSCAPE_DIRECTORY: join(folder, 'scenes'), ATLAS_EXPORT_DIRECTORY: join(folder, 'exports') }, stdio: ['pipe', 'pipe', 'pipe'] });
  closed = new Promise(done => server.once('exit', done));
  server.stdout.resume(); server.stderr.resume();
  let ready = false;
  for (let attempt = 0; attempt < 100; attempt++) {
    if (server.exitCode !== null) throw Error('Isolated server exited');
    try { if ((await fetch(origin)).ok) { ready = true; break; } } catch {}
    await new Promise(done => setTimeout(done, 50));
  }
  assert.equal(ready, true, 'Isolated Vite server did not start');
  browser = await chromium.launch({ headless: true, channel: process.env.ATLAS_BROWSER || 'chrome', args: ['--enable-unsafe-swiftshader'] });
  page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  page.on('pageerror', error => errors.push(error.message));
  page.on('response', async response => {
    if (!response.url().includes('/__atlas/request')) return;
    const request = response.request().postDataJSON();
    try {
      const reply = await response.json();
      if (!reply.ok) rejected.push({ method: request.method, error: reply.error });
      else replies.push({ method: request.method, result: reply.result });
      if (['setAudioDevice', 'setAudioEnabled', 'setTransport'].includes(request.method)) mutations.push({ method: request.method, args: request.args });
    } catch { /* A superseded read has no observable state. */ }
  });
  const button = name => page.getByRole('button', { name, exact: true });
  const listening = () => page.getByRole('switch', { name: /Listening/ });
  await page.goto(origin);
  await button('Audio settings').waitFor();
  await page.getByRole('radio', { name: 'Replay', exact: true }).click();
  await page.getByRole('button', { name: /Fixture • 2,005 records/ }).click();
  await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor();
  await listening().click(); await button('Play').click(); await button('Pause playback').waitFor();
  await button('Audio settings').click();
  const outputs = page.getByRole('combobox', { name: 'Output device', exact: true });
  await outputs.selectOption('device:fixture-external');
  await page.getByRole('combobox', { name: 'Buffer size', exact: true }).selectOption('256');
  assert.equal(replies.filter(reply => reply.method === 'setAudioDevice').length, 0);
  const before = mutations.length;
  await button('Apply output').click();
  await page.getByRole('status').filter({ hasText: 'Output applied. Listening is off.' }).waitFor();
  assert.equal(await button('Play').isDisabled(), true); assert.equal(await listening().isChecked(), false);
  const applied = replies.filter(reply => reply.method === 'setAudioDevice').at(-1).result;
  assert.deepEqual(applied.configuration, { mode: 'explicitDevice', deviceId: 'fixture-external', bufferFrames: 256 });
  assert.equal(applied.active, null);
  assert.equal(mutations.slice(before).some(call => call.method === 'setAudioEnabled' && call.args.enabled || call.method === 'setTransport' && call.args.action === 'play'), false);
  assert.equal((await post('getAudioState', {})).result.status, 'disabled');
  const transport = (await post('getTransport', {})).result;
  assert.equal(transport.state, 'stopped'); assert.equal(transport.generation, applied.generation);
  // Another client changes the same native control fence; this UI must not overwrite it.
  const changed = await post('setAudioDevice', { controlRevision: applied.controlRevision, preparationId: applied.preparationId, generation: applied.generation, configuration: { mode: 'systemDefault', deviceId: null, bufferFrames: 0 } });
  assert.equal(changed.ok, true);
  await button('Apply output').click();
  await page.getByRole('status').filter({ hasText: 'Refresh devices, then apply again.' }).waitFor();
  await button('Refresh devices').click();
  await outputs.selectOption('device:fixture-external');
  await page.getByRole('combobox', { name: 'Buffer size', exact: true }).selectOption('128');
  await button('Apply output').click();
  await page.getByRole('status').filter({ hasText: 'Output applied. Listening is off.' }).waitFor();
  await page.keyboard.press('Escape');
  await listening().click(); await button('Play').click(); await button('Pause playback').waitFor();
  await button('Audio settings').click();
  const active = await post('getAudioDevices', {});
  assert.equal(active.ok, true); assert.equal(active.result.active.deviceId, 'fixture-external');
  const audio = (await post('getAudioState', {})).result;
  assert.equal(audio.status, 'running'); assert.ok(Number(audio.callbackCount) > 0);
  await page.getByText(new RegExp(`${active.result.active.sampleRate.toLocaleString()} Hz · ${active.result.active.bufferFrames} frames`)).waitFor();
  await page.evaluate(() => document.fonts.ready);
  await mkdir(join(app, 'test-results'), { recursive: true });
  await page.screenshot({ path: join(app, 'test-results/native-audio-device-preferences.png') });
  await page.keyboard.press('Escape'); await button('All off').click();
  assert.deepEqual(errors, []);
  assert.deepEqual(rejected.map(item => ({ method: item.method, code: item.error.code })), [{ method: 'setAudioDevice', code: 'stale_generation' }]);
  const result = { startedAtUtc: new Date().toISOString(), scope: 'Real native protocol and fake output; no physical audio, owner database or owner state', binary, binarySha256: createHash('sha256').update(await readFile(binary)).digest('hex'), browser: browser.version(), errors, rejected, checks: ['real native device catalog decoded by React', 'explicit device and buffer draft stays local', 'apply while playing stops and disables', 'fresh transport generation and no automatic audio/play', 'competing native apply rejects stale UI fence', 'refresh and reapply recovers', 'manual Listening and Play reach configured fake output', 'truthful callback progress and active rate/buffer'], applied: replies.filter(reply => reply.method === 'setAudioDevice').map(reply => reply.result), audio, screenshot: 'test-results/native-audio-device-preferences.png' };
  await writeFile(join(app, 'test-results/native-audio-devices-browser.json'), JSON.stringify(result, null, 2) + '\n');
  console.log(JSON.stringify(result));
} catch (error) { if (page) console.error(JSON.stringify({ alerts: await page.getByRole('alert').allTextContents(), rejected, replies: replies.slice(-8) })); throw error; }
finally { if (browser) await browser.close(); await stop(); await rm(folder, { recursive: true, force: true }); }
