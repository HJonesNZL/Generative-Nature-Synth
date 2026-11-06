// MANUAL REAL-DEVICE TEST. Never part of npm test. Requires explicit authorization.
import assert from 'node:assert/strict';
import { spawn, execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFile, writeFile, mkdir, realpath } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
if (process.env.ATLAS_REAL_AUDIO !== '1') throw new Error('Real audio disabled. Set ATLAS_REAL_AUDIO=1 only for an explicitly authorized bounded device smoke.');
const app = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const root = resolve(app, '../..');
const library = await realpath(process.env.ATLAS_LIBRARY || '');
if (!/\/atlas-real-ui-smoke-[^/]+\/synthetic\.sqlite3$/.test(library)) throw new Error('Use a newly created atlas-real-ui-smoke-* temporary synthetic library.');
const binary = resolve(process.env.ATLAS_NATIVE_BINARY || resolve(root, 'build/native-playback/atlas-audio-host'));
const hash = path => readFile(path).then(bytes => createHash('sha256').update(bytes).digest('hex'));
const port = Number(process.env.ATLAS_DEVICE_SMOKE_PORT || '5184');
const origin = `http://127.0.0.1:${port}`;
const result = { startedAtUtc: new Date().toISOString(), binary, binarySha256: await hash(binary), library, origin, commands: [], snapshots: [], audioStates: [], pageErrors: [], externalRequests: [], scope: 'Actual browser -> devbridge -> atlas-audio-host -> default physical output, synthetic saved piece, fixed native -18dB attenuation. No device/volume change, loops or subjective listening assertion.' };
let browser, page, timer, killTimer, playingAt, quiet = false;
const childPids = new Set();
const server = spawn(process.execPath, ['node_modules/vite/bin/vite.js', '--port', String(port)], { cwd: app, env: { ...process.env, ATLAS_NATIVE_BINARY: binary, ATLAS_LIBRARY: library, ATLAS_VOICE_LIBRARY: resolve(dirname(library), 'voices.sqlite3') }, stdio: ['pipe', 'pipe', 'pipe'] });
server.stdout.resume(); server.stderr.resume();
const closed = new Promise(resolveExit => server.once('exit', (code, signal) => resolveExit({ code, signal })));
const discoverChildren = () => {
  try { for (const value of execFileSync('/usr/bin/pgrep', ['-P', String(server.pid)], { encoding: 'utf8' }).trim().split(/\s+/)) if (/^\d+$/.test(value)) childPids.add(Number(value)); } catch {}
};
const alive = pid => { try { process.kill(pid, 0); return true; } catch { return false; } };
let id = 0;
const post = async (method, args) => {
  const response = await fetch(`${origin}/__atlas/request`, { method: 'POST', headers: { Origin: origin, 'Content-Type': 'application/json' }, body: JSON.stringify({ protocolVersion: 1, requestId: `device-smoke-${++id}`, method, args }), signal: AbortSignal.timeout(1500) });
  const reply = await response.json();
  result.commands.push({ method, args, reply, atMs: playingAt ? performance.now() - playingAt : null });
  return reply;
};
const disable = async () => {
  try { const reply = await post('setAudioEnabled', { enabled: false }); quiet = reply.ok && reply.result.status === 'disabled'; } catch (error) { result.safetyError = error.message; }
};
try {
  for (let attempt = 0; attempt < 100; attempt++) {
    if (server.exitCode !== null) throw new Error('Isolated preview exited before ready');
    try { const response = await fetch(origin); if (response.ok) break; } catch {}
    await new Promise(resolveWait => setTimeout(resolveWait, 50));
  }
  const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
  browser = await chromium.launch({ headless: true, channel: 'chrome', args: ['--enable-unsafe-swiftshader'] });
  result.browser = browser.version();
  page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  page.setDefaultTimeout(3000);
  page.on('pageerror', error => result.pageErrors.push(error.message));
  page.on('request', request => { if (/^https?:/.test(request.url()) && !request.url().startsWith(origin)) result.externalRequests.push(request.url()); });
  page.on('response', async response => {
    if (!response.url().includes('/__atlas/request')) return;
    try {
      const request = response.request().postDataJSON();
      const reply = await response.json();
      if (request.method === 'getTransport' && reply.ok) result.snapshots.push(reply.result);
      if (request.method === 'getAudioState' && reply.ok) result.audioStates.push(reply.result);
      if (['prepareReplay', 'setAudioEnabled', 'setTransport'].includes(request.method)) result.commands.push({ ...request, reply, atMs: playingAt ? performance.now() - playingAt : null });
    } catch {}
  });
  await page.goto(origin);
  await page.getByRole('radio', { name: 'Replay', exact: true }).click();
  await page.getByRole('button', { name: /Synthetic three second device smoke/ }).click();
  await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor();
  await page.getByRole('radio', { name: '1×', exact: true }).click();
  await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor();
  await page.getByRole('switch', { name: 'Listening (audio off)' }).click();
  discoverChildren();
  const device = await post('getAudioState', {});
  assert.equal(device.ok, true); assert.equal(device.result.status, 'running'); assert.equal(device.result.masterGainDb, -18);
  playingAt = performance.now();
  // Independent deadline: disable at 1.1s; if unacknowledged kill only the owned
  // native child at 1.3s, bounding a failed UI interaction below 1.5s.
  timer = setTimeout(() => { result.watchdogTriggered = true; void disable(); }, 1100);
  killTimer = setTimeout(() => { if (!quiet) { result.watchdogKilledChild = true; discoverChildren(); for (const pid of childPids) { try { process.kill(pid, 'SIGKILL'); } catch {} } } }, 1300);
  await page.getByRole('button', { name: 'Play', exact: true }).click();
  for (let attempt = 0; attempt < 100 && !result.snapshots.some(s => s.state === 'playing' && BigInt(s.positionUs) >= 300000n); attempt++) await new Promise(resolveWait => setTimeout(resolveWait, 10));
  assert.ok(result.snapshots.some(s => s.state === 'playing' && BigInt(s.positionUs) >= 300000n), 'Native browser poll did not publish >0.3 seconds');
  await page.getByRole('button', { name: 'Pause playback', exact: true }).click();
  let paused;
  for (let attempt = 0; attempt < 20; attempt++) {
    const reply = await post('getTransport', {});
    if (reply.ok && reply.result.state === 'paused') { paused = reply.result; break; }
    await new Promise(resolveWait => setTimeout(resolveWait, 10));
  }
  assert.ok(paused, 'Native pause fade did not complete');
  await new Promise(resolveWait => setTimeout(resolveWait, 80));
  const stable = await post('getTransport', {});
  assert.equal(stable.result.state, 'paused'); assert.equal(stable.result.positionUs, paused.positionUs);
  result.pausedPositionUs = paused.positionUs;
  await page.getByRole('button', { name: 'Stop playback', exact: true }).click();
  await page.getByRole('switch', { name: 'Listening (audio on)' }).click();
  const stopped = await post('getTransport', {}), off = await post('getAudioState', {});
  assert.equal(stopped.result.state, 'stopped'); assert.equal(stopped.result.positionUs, '0'); assert.equal(off.result.status, 'disabled');
  quiet = true;
  result.elapsedToAudioDisabledMs = performance.now() - playingAt;
  assert.ok(result.elapsedToAudioDisabledMs < 1500);
  assert.deepEqual(result.pageErrors, []); assert.deepEqual(result.externalRequests, []);
  result.status = 'passed';
} catch (error) {
  result.status = 'failed'; result.failure = error.stack;
} finally {
  if (playingAt && !quiet) await disable();
  clearTimeout(timer); clearTimeout(killTimer);
  discoverChildren();
  if (page) page.removeAllListeners('response');
  if (browser) await browser.close();
  server.stdin.destroy(); server.kill('SIGTERM');
  result.serverExit = await Promise.race([closed, new Promise(resolveWait => setTimeout(() => resolveWait(null), 1500))]);
  if (!result.serverExit) { server.kill('SIGKILL'); result.serverExit = await closed; }
  for (let attempt = 0; attempt < 50 && [...childPids].some(alive); attempt++) await new Promise(resolveWait => setTimeout(resolveWait, 20));
  result.ownedChildPids = [...childPids]; result.ownedChildrenGone = [...childPids].every(pid => !alive(pid));
  result.binarySha256After = await hash(binary);
  if (!result.ownedChildrenGone || result.binarySha256After !== result.binarySha256) { result.status = 'failed'; result.teardownFailure = 'Owned child remains or binary changed'; }
  const output = resolve(root, 'evidence/native-playback/ui-device-smoke.json');
  await mkdir(dirname(output), { recursive: true });
  let history = { runs: [] };
  try { history = JSON.parse(await readFile(output, 'utf8')); } catch {}
  history.runs.push(result);
  await writeFile(output, JSON.stringify(history, null, 2) + '\n');
  console.log(JSON.stringify({ status: result.status, binarySha256: result.binarySha256, pausedPositionUs: result.pausedPositionUs, elapsedToAudioDisabledMs: result.elapsedToAudioDisabledMs, ownedChildrenGone: result.ownedChildrenGone, evidence: output, failure: result.failure }));
}
if (result.status !== 'passed') process.exitCode = 1;
