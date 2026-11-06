// Real native scene capture/recall/render via a silent fixture driver; all state is disposable.
import assert from 'node:assert/strict';
import { spawn, execFileSync } from 'node:child_process';
import { mkdtemp, rm, readFile, mkdir, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, resolve, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const app = resolve(dirname(fileURLToPath(import.meta.url)), '..'), root = resolve(app, '../..');
const folder = await mkdtemp(join(tmpdir(), 'atlas-soundscapes-ui-'));
const binary = process.env.ATLAS_STUDIO_FIXTURE_BINARY || resolve(root, 'build/native/atlas-studio-fixture-host');
const port = Number(process.env.ATLAS_SOUNDSCAPE_TEST_PORT || 5193), origin = `http://127.0.0.1:${port}`;
const library = join(folder, 'synthetic.sqlite3'), voices = join(folder, 'voices.sqlite3');
const knownPreset = process.env.ATLAS_TEST_KNOWN_PRESET === '1';
const fixtureName = knownPreset ? 'Fixture House sparrow' : 'Fixture Tūī';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
let browser, server, closed;
const errors = [], rejected = [], replies = [], mutations = [];
const button = (page, name) => page.getByRole('button', { name, exact: true });
async function start() {
  server = spawn(process.execPath, ['node_modules/vite/bin/vite.js', '--port', String(port), '--strictPort'], { cwd: app,
    env: { ...process.env, ATLAS_AUDIO_HOST_BINARY: '', ATLAS_NATIVE_BINARY: binary, ATLAS_LIBRARY: library, ATLAS_VOICE_LIBRARY: voices,
      ATLAS_SOUNDSCAPE_DIRECTORY: join(folder, 'scenes'), ATLAS_EXPORT_DIRECTORY: join(folder, 'exports') }, stdio: ['pipe', 'pipe', 'pipe'] });
  closed = new Promise(done => server.once('exit', done)); server.stdout.resume(); server.stderr.resume();
  for (let attempt = 0; attempt < 100; attempt++) {
    if (server.exitCode !== null) throw Error('Isolated UI server exited');
    try { if ((await fetch(origin)).ok) return; } catch {}
    await new Promise(done => setTimeout(done, 50));
  }
  throw Error('Isolated UI server did not start');
}
async function exportSaved(page) {
  const previous = replies.filter(r => r.method === 'startSoundscapeRender').length;
  const previousArtifact = await page.locator('a[download]').count() ? await page.locator('a[download]').getAttribute('data-artifact-id') : null;
  await button(page, 'Export saved WAV').click();
  await page.waitForFunction(previousArtifact => { const link = document.querySelector('a[download]'); return link && link.getAttribute('data-artifact-id') !== previousArtifact; }, previousArtifact, { timeout: 30000 });
  await page.waitForTimeout(50);
  const started = replies.filter(r => r.method === 'startSoundscapeRender'); assert.equal(started.length, previous + 1);
  const download = page.getByRole('link', { name: 'Download WAV', exact: true });
  const response = await page.request.get(new URL(await download.getAttribute('href'), origin).href);
  assert.equal(response.status(), 200); const bytes = await response.body();
  assert.equal(bytes.subarray(0, 4).toString(), 'RIFF'); assert.equal(bytes.subarray(8, 12).toString(), 'WAVE');
  return bytes;
}
try {
  if (knownPreset) {
    const source = (await readFile(join(root, 'tests/ui_fixture.py'), 'utf8'))
      .replace("species_id='fixture:species:tui'", "species_id='birdweather:species:1'").replaceAll('Fixture Tūī', fixtureName);
    execFileSync('python3', ['-B', '-', library], { cwd: root, input: `__file__ = ${JSON.stringify(join(root, 'tests/ui_fixture.py'))}\n${source}`, stdio: ['pipe', 'pipe', 'pipe'] });
  } else execFileSync('python3', ['-B', 'tests/ui_fixture.py', library], { cwd: root, stdio: 'pipe' });
  await mkdir(join(folder, "scenes"), { recursive: true });
  await writeFile(join(folder, "scenes", "unsupported-name.atlas-scene"), "Synthetic unmanaged filename fixture");
  await start(); browser = await chromium.launch({ headless: true, channel: 'chrome', args: ['--enable-unsafe-swiftshader'] });
  const page = await browser.newPage({ viewport: { width: 1700, height: 1100 } }); page.setDefaultTimeout(15000);
  page.on('pageerror', error => errors.push(error.message));
  page.on('response', async response => {
    if (!response.url().includes('/__atlas/request')) return;
    try { const request = response.request().postDataJSON(), reply = await response.json();
      if (!reply.ok) rejected.push({ method: request.method, error: reply.error });
      else { replies.push({ method: request.method, result: reply.result }); if (['setTransport', 'setAudioEnabled', 'startSoundscapeRender', 'recallSoundscape'].includes(request.method)) mutations.push(request); }
    } catch {}
  });
  await page.goto(origin); await page.getByRole('region', { name: 'Soundscapes', exact: true }).waitFor();
  await page.getByText('1 file uses an unsupported name and cannot be opened here.', { exact: true }).waitFor();
  await page.getByRole('radio', { name: 'Replay', exact: true }).click();
  await page.getByRole('button', { name: /^Fixture • 2,005 records/ }).click();
  await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor();
  await page.getByRole('radio', { name: '240×', exact: true }).click();
  await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor();
  await button(page, 'Sound Studio').click();
  await page.getByRole('button', { name: new RegExp(`^${fixtureName} species voice`) }).click();
  if (knownPreset) {
    const selector = page.getByRole('combobox', { name: 'Experimental voice preset', exact: true });
    await page.waitForFunction(() => (document.querySelector('select[aria-label="Experimental voice preset"]')?.options.length ?? 0) > 1);
    const baseline = replies.filter(reply => reply.method === 'getVoice').at(-1).result;
    await page.getByRole('slider', { name: 'Brightness', exact: true }).press('Home');
    await page.getByText('Unsaved changes', { exact: true }).waitFor();
    const presetId = await selector.locator('option').nth(1).getAttribute('value');
    await selector.selectOption(presetId); await button(page, 'Apply to voice draft').click();
    await page.getByText('Experimental preset applied to this voice draft. Save voice or save a soundscape to keep it.', { exact: true }).waitFor();
    const applied = replies.filter(reply => reply.method === 'applyVoicePreset').at(-1).result;
    assert.deepEqual(applied.values, baseline.values); assert.equal(applied.savedRevision, baseline.savedRevision);
    assert.equal(replies.filter(reply => reply.method === 'saveVoice').length, 0);
    assert.ok(!mutations.some(request => request.method === 'setTransport' && request.args.action === 'play'));
  }
  const transpose = page.getByRole('slider', { name: 'Transpose', exact: true });
  await transpose.press('End');
  await page.getByText('Unsaved changes', { exact: true }).waitFor();
  await page.getByRole('textbox', { name: 'Soundscape name', exact: true }).fill('Scene captured at +12');
  await button(page, 'Save soundscape').click();
  await page.getByText(/Saved “Scene captured at \+12” with/).waitFor();
  const saved = replies.filter(r => r.method === 'saveSoundscape').at(-1).result;
  assert.equal(saved.valid, true); assert.equal(saved.eventCount, 2005);
  await transpose.press('Home'); await page.waitForFunction(() => document.querySelector('[role="slider"][aria-label="Transpose"]')?.getAttribute('aria-valuetext') === '-12 st');
  const beforeRecall = await exportSaved(page);
  assert.equal(mutations.filter(r => r.method === 'startSoundscapeRender').at(-1).args.soundscapeId, saved.id);
  const beforeMutations = mutations.length;
  await button(page, 'Open soundscape').click();
  await page.getByText(/Opened “Scene captured at \+12”/).waitFor();
  assert.equal(await page.getByRole('switch', { name: 'Listening (audio off)', exact: true }).getAttribute('aria-checked'), 'false');
  assert.equal(await button(page, 'Play').isDisabled(), true);
  assert.ok(!mutations.slice(beforeMutations).some(r => r.method === 'setTransport' && r.args.action === 'play'));
  const recall = replies.filter(r => r.method === 'recallSoundscape').at(-1).result;
  assert.equal(recall.preparation.session.id, `scene:${saved.id}`);
  await page.getByRole('button', { name: new RegExp(`^${fixtureName} species voice`) }).click();
  await page.waitForFunction(() => document.querySelector('[role="slider"][aria-label="Transpose"]')?.getAttribute('aria-valuetext') === '+12 st');
  const afterRecall = await exportSaved(page); assert.deepEqual(afterRecall, beforeRecall, 'immutable saved WAV must ignore later drafts');
  // Re-preparing the same scene at another speed preserves current edits.
  await page.getByRole('slider', { name: 'Transpose', exact: true }).press('Home');
  await button(page, 'Atlas').click(); await page.getByRole('radio', { name: '60×', exact: true }).click();
  await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor();
  await button(page, 'Sound Studio').click();
  await page.waitForFunction(() => document.querySelector('[role="slider"][aria-label="Transpose"]')?.getAttribute('aria-valuetext') === '-12 st');
  await page.screenshot({ path: process.env.ATLAS_SOUNDSCAPE_SCREENSHOT || '/private/tmp/atlas-soundscape-ui.png', fullPage: true });
  assert.deepEqual(errors, []); assert.deepEqual(rejected, []);
  console.log(JSON.stringify({ checks: [...(knownPreset ? ['known-species preset discovery/application without save/play'] : []), 'unmanaged filename notice coexists with valid scene', 'full native capture after voice edits', 'scene-only WAV export despite later draft edits', 'app-owned WAV download', 'atomic recall restoring voice and copied scene context', 'recall remains silent', 'same-scene speed preparation preserves current draft'], knownSpeciesPresetApplied: knownPreset, savedId: saved.id, wavBytes: beforeRecall.length, pageErrors: errors, rejected }));
} catch (error) { console.error(JSON.stringify({ rejected, recent: replies.slice(-5), mutations })); throw error; }
finally {
  await browser?.close();
  if (server) { server.kill('SIGTERM'); const stopped = await Promise.race([closed.then(() => true), new Promise(done => setTimeout(() => done(false), 2000))]); if (!stopped) { server.kill('SIGKILL'); await closed; } }
  await rm(folder, { recursive: true, force: true });
}
