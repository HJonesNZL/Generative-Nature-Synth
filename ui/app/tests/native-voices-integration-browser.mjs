// Real C++ UiHost/VoiceStore via a silent fixture host. Entire state is temporary.
import assert from 'node:assert/strict';
import { spawn, execFileSync } from 'node:child_process';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, resolve, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const app = resolve(dirname(fileURLToPath(import.meta.url)), '..'), root = resolve(app, '../..');
const folder = await mkdtemp(join(tmpdir(), 'atlas-native-voices-ui-'));
const binary = process.env.ATLAS_STUDIO_FIXTURE_BINARY || resolve(root, 'build/native/atlas-studio-fixture-host');
const port = Number(process.env.ATLAS_NATIVE_VOICES_TEST_PORT || 5188), origin = `http://127.0.0.1:${port}`;
const library = join(folder, 'synthetic.sqlite3'), voices = join(folder, 'voices.sqlite3');
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
let browser, server, closed;
const errors = [], rejected = [], replies = [];
async function start() {
  server = spawn(process.execPath, ['node_modules/vite/bin/vite.js', '--port', String(port)], { cwd: app,
    env: { ...process.env, ATLAS_AUDIO_HOST_BINARY: '', ATLAS_NATIVE_BINARY: binary, ATLAS_LIBRARY: library, ATLAS_VOICE_LIBRARY: voices, ATLAS_SOUNDSCAPE_DIRECTORY: join(folder, "scenes"), ATLAS_EXPORT_DIRECTORY: join(folder, "exports") }, stdio: ['pipe', 'pipe', 'pipe'] });
  closed = new Promise(done => server.once('exit', done));
  server.stdout.resume(); server.stderr.resume();
  for (let attempt = 0; attempt < 100; attempt++) {
    if (server.exitCode !== null) throw Error('Isolated server exited');
    try { if ((await fetch(origin)).ok) return; } catch {}
    await new Promise(done => setTimeout(done, 50));
  }
  throw Error('Isolated server did not start');
}
async function stop() {
  if (!server) return;
  server.stdin.destroy(); server.kill('SIGTERM');
  const stopped = await Promise.race([closed.then(() => true), new Promise(done => setTimeout(() => done(false), 2000))]);
  if (!stopped) { server.kill('SIGKILL'); await closed; }
  server = null;
}
async function open() {
  const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  page.on('pageerror', error => errors.push(error.message));
  page.on('response', async response => {
    if (!response.url().includes('/__atlas/request')) return;
    try { const request = response.request().postDataJSON(), reply = await response.json();
      if (!reply.ok) rejected.push({ method: request.method, error: reply.error });
      else replies.push({ method: request.method, result: reply.result });
    } catch {}
  });
  await page.goto(origin); await page.getByRole('button', { name: 'Sound Studio', exact: true }).click();
  await page.getByRole('button', { name: /^Fixture Tūī species voice/ }).click();
  await page.getByRole('heading', { name: 'Fixture Tūī', exact: true }).waitFor();
  return page;
}
try {
  execFileSync('python3', ['-B', 'tests/ui_fixture.py', library], { cwd: root, stdio: 'pipe' });
  await start();
  browser = await chromium.launch({ headless: true, channel: 'chrome', args: ['--enable-unsafe-swiftshader'] });
  let page = await open();
  assert.equal(await page.locator('.studio__center [role="slider"]').count(), 8);
  await page.getByRole('radio', { name: 'struck', exact: true }).waitFor();
  assert.equal(await page.getByRole('button', { name: 'Audition selected event', exact: true }).isDisabled(), true);
  await page.getByRole('slider', { name: 'Transpose', exact: true }).press('End');
  await page.getByText('Unsaved changes', { exact: true }).waitFor();
  await page.getByRole('button', { name: 'Save voice', exact: true }).click();
  await page.waitForFunction(() => /^Saved rev \d+$/.test(document.querySelector('.studio__center .row-between .label:last-child')?.textContent ?? ''));
  assert.equal(await page.getByRole('slider', { name: 'Transpose', exact: true }).getAttribute('aria-valuetext'), '+12 st');
  assert.equal(replies.filter(r => r.method === 'saveVoice').at(-1).result.dirty, false);
  // Leave an unsaved coefficient edit; process restart must restore saved only.
  await page.getByRole('slider', { name: 'Attack', exact: true }).press('End');
  await page.getByText('Unsaved changes', { exact: true }).waitFor();
  await page.close(); await stop(); await start(); page = await open();
  assert.equal(await page.getByRole('slider', { name: 'Transpose', exact: true }).getAttribute('aria-valuetext'), '+12 st');
  assert.equal(await page.getByRole('slider', { name: 'Attack', exact: true }).getAttribute('aria-valuetext'), '8 ms');
  assert.equal(await page.getByRole('button', { name: 'Save voice', exact: true }).isDisabled(), true);
  await page.getByRole('slider', { name: 'Transpose', exact: true }).press('Home');
  await page.getByRole('button', { name: 'Revert', exact: true }).click();
  await page.waitForFunction(() => document.querySelector('[role="slider"][aria-label="Transpose"]')?.getAttribute('aria-valuetext') === '+12 st');
  await page.getByRole('button', { name: 'Fallback voice', exact: true }).click();
  await page.getByRole('heading', { name: 'Fallback voice', exact: true }).waitFor();
  assert.match(await page.locator('.studio__center').innerText(), /Fallback for animals without a species voice/);
  assert.doesNotMatch(await page.locator('.studio__center').innerText(), /Species without their own voice use it/);
  assert.equal(await page.getByRole('slider', { name: 'Transpose', exact: true }).getAttribute('aria-valuetext'), '+0 st');
  assert.deepEqual(errors, []); assert.deepEqual(rejected, []);
  console.log(JSON.stringify({ checks: ['real native library metadata and eight controls', 'native canonical acknowledgements and Save', 'owned host process restart preserves saved values and discards drafts', 'native Revert', 'independent fallback definition'], pageErrors: errors, rejected }));
} finally { if (browser) await browser.close(); await stop(); await rm(folder, { recursive: true, force: true }); }
