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
const composition = process.env.ATLAS_TEST_COMPOSITION === '1';
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
  let releaseComposition, compositionSeen, acknowledgeComposition;
  const compositionGate = new Promise(done => { acknowledgeComposition = done; });
  if (composition) {
    compositionSeen = new Promise(done => { releaseComposition = done; });
    await page.route('**/__atlas/request', async route => {
      const request = route.request().postDataJSON();
      if (request.method !== 'setComposition') return route.continue();
      const response = await route.fetch(); releaseComposition();
      await compositionGate;
      return route.fulfill({ response });
    });
  }
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
  let compositionSettings, oldPreparation;
  if (composition) {
    oldPreparation = replies.filter(r => r.method === 'getComposition').at(-1).result.preparation;
    await page.getByRole('checkbox', { name: 'Enable composition', exact: true }).check();
    await page.getByRole('spinbutton', { name: 'Composition tempo BPM', exact: true }).fill('');
    await page.getByRole('spinbutton', { name: 'Composition tempo BPM', exact: true }).pressSequentially('117');
    assert.equal(await page.getByRole('spinbutton', { name: 'Composition tempo BPM', exact: true }).inputValue(), '117');
    await page.getByRole('combobox', { name: 'Composition tonic', exact: true }).selectOption('8');
    await page.getByRole('combobox', { name: 'Composition scale', exact: true }).selectOption('naturalMinor');
    await page.getByRole('combobox', { name: 'Composition density', exact: true }).selectOption('2');
    await page.getByRole('spinbutton', { name: 'Composition seed', exact: true }).fill('');
    await page.getByRole('spinbutton', { name: 'Composition seed', exact: true }).pressSequentially('4294967295');
    assert.equal(await page.getByRole('spinbutton', { name: 'Composition seed', exact: true }).inputValue(), '4294967295');
    await page.getByRole('checkbox', { name: 'Quantize composition', exact: true }).uncheck();
    await button(page, 'Atlas').click(); await button(page, 'Sound Studio').click();
    assert.equal(await page.getByRole('spinbutton', { name: 'Composition tempo BPM', exact: true }).inputValue(), '117');
    assert.equal(replies.filter(r => r.method === 'setComposition').length, 0, 'draft waits for explicit apply or save');
  }
  await page.getByRole('textbox', { name: 'Soundscape name', exact: true }).fill('Scene captured at +12');
  if (composition) {
    await page.getByRole('spinbutton', { name: 'Composition tempo BPM', exact: true }).fill('39');
    await button(page, 'Save soundscape').click();
    await page.getByText('Enter a whole tempo from 40 to 180 BPM before applying or saving.', { exact: true }).waitFor();
    assert.equal(replies.filter(r => r.method === 'saveSoundscape').length, 0, 'invalid text draft cannot be captured');
    assert.equal(replies.filter(r => r.method === 'setComposition').length, 0, 'invalid text draft is not sent');
    await page.getByRole('spinbutton', { name: 'Composition tempo BPM', exact: true }).fill('');
    await page.getByRole('spinbutton', { name: 'Composition tempo BPM', exact: true }).pressSequentially('117');
  }
  await button(page, 'Save soundscape').click();
  if (composition) {
    await compositionSeen;
    assert.equal(replies.filter(r => r.method === 'saveSoundscape').length, 0, 'capture waits for delayed composition acknowledgement');
    assert.equal(await button(page, 'Saving…').isDisabled(), true);
    assert.equal(await page.getByRole('spinbutton', { name: 'Composition tempo BPM', exact: true }).isDisabled(), true);
    acknowledgeComposition();
  }
  await page.getByText(/Saved “Scene captured at \+12” with/).waitFor();
  const saved = replies.filter(r => r.method === 'saveSoundscape').at(-1).result;
  assert.equal(saved.valid, true); assert.equal(saved.eventCount, 2005);
  if (composition) {
    const accepted = replies.filter(r => r.method === 'setComposition').at(-1).result;
    compositionSettings = accepted.settings;
    assert.ok(Number.isInteger(accepted.generatedNotes) && accepted.generatedNotes > 0);
    assert.ok(Number.isInteger(accepted.suppressedSourceEvents) && accepted.suppressedSourceEvents > 0);
    assert.ok(Number.isInteger(accepted.suppressed) && accepted.suppressed >= 0);
    assert.deepEqual(compositionSettings, { enabled: true, tempoBpm: 117, tonicPitchClass: 8, scale: 'naturalMinor', density: 2, variation: .25, seed: 4294967295, quantize: false });
    assert.equal(await button(page, 'Play').isDisabled(), true);
    const raw = async (settings, preparation = accepted.preparation) => {
      const response = await page.request.post(`${origin}/__atlas/request`, { headers: { origin }, data: { protocolVersion: 1, requestId: `composition-probe-${Math.random()}`, method: 'setComposition', args: { settings, preparationId: preparation.preparationId, generation: preparation.generation } } });
      return response.json();
    };
    assert.equal((await raw(compositionSettings, oldPreparation)).error.code, 'stale_generation');
    for (const patch of [{ tempoBpm: 39 }, { tempoBpm: 117.5 }, { seed: 4294967296 }, { tonicPitchClass: 12 }, { density: 3 }, { scale: 'unknown' }, { variation: 1.1 }, { quantize: 1 }, { extra: true }]) {
      assert.equal((await raw({ ...compositionSettings, ...patch })).error.code, 'invalid_request');
    }
    const voiceProbe = await page.request.post(`${origin}/__atlas/request`, { headers: { origin }, data: { protocolVersion: 1, requestId: 'composition-voice-read', method: 'getVoice', args: { voiceId: replies.filter(r => r.method === 'getVoice').at(-1).result.id } } });
    const heldVoice = (await voiceProbe.json()).result;
    const voiceGesture = async gesture => {
      const response = await page.request.post(`${origin}/__atlas/request`, { headers: { origin }, data: { protocolVersion: 1, requestId: `composition-voice-${gesture}`, method: 'setVoiceParameter', args: { voiceId: heldVoice.id, parameterId: 'tuning', normalizedValue: heldVoice.values.tuning, gesture } } });
      assert.equal((await response.json()).ok, true);
    };
    await voiceGesture('begin');
    assert.equal((await raw(compositionSettings)).error.code, 'invalid_request', 'active voice gesture blocks full composition');
    await voiceGesture('end');
    const probe = await page.request.post(`${origin}/__atlas/request`, { headers: { origin }, data: { protocolVersion: 1, requestId: 'composition-read', method: 'getComposition', args: {} } });
    const unchanged = await probe.json(); assert.deepEqual(unchanged.result.settings, compositionSettings); assert.equal(unchanged.result.preparation.preparationId, accepted.preparation.preparationId);
    await page.getByRole('spinbutton', { name: 'Composition tempo BPM', exact: true }).fill('123');
    await button(page, 'Apply composition').click();
    await page.getByText('Composition applied. Listening is off; enable it and press Play to hear this candidate.', { exact: true }).waitFor();
  }
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
  if (composition) {
    await page.waitForFunction(() => document.querySelector('input[aria-label="Composition tempo BPM"]')?.value === '117');
    assert.equal(await page.getByRole('combobox', { name: 'Composition tonic', exact: true }).inputValue(), '8');
    assert.equal(await page.getByRole('combobox', { name: 'Composition scale', exact: true }).inputValue(), 'naturalMinor');
    assert.equal(await page.getByRole('spinbutton', { name: 'Composition seed', exact: true }).inputValue(), '4294967295');
    assert.deepEqual(replies.filter(r => r.method === 'getComposition').at(-1).result.settings, compositionSettings);
  }
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
  if (composition) {
    await page.waitForFunction(() => !document.querySelector('fieldset.composition-panel__controls')?.disabled);
    assert.equal(await page.getByRole('spinbutton', { name: 'Composition tempo BPM', exact: true }).inputValue(), '117', 'same-piece speed change preserves composition');
  }
  assert.deepEqual(errors, []); assert.deepEqual(rejected, []);
  console.log(JSON.stringify({ checks: [...(composition ? ['sequential tempo and uint32 seed keyboard entry', 'invalid numeric text draft blocks native mutation and capture', 'atomic full composition draft across pages', 'save waits for delayed composition acknowledgement', 'invalid/stale composition rejection without mutation', 'composition exact recall and immutable WAV export', 'composition preserved during speed changes'] : []), ...(knownPreset ? ['known-species preset discovery/application without save/play'] : []), 'unmanaged filename notice coexists with valid scene', 'full native capture after voice edits', 'scene-only WAV export despite later draft edits', 'app-owned WAV download', 'atomic recall restoring voice and copied scene context', 'recall remains silent', 'same-scene speed preparation preserves current draft'], knownSpeciesPresetApplied: knownPreset, savedId: saved.id, wavBytes: beforeRecall.length, pageErrors: errors, rejected }));
} catch (error) { console.error(JSON.stringify({ rejected, recent: replies.slice(-5), mutations })); throw error; }
finally {
  await browser?.close();
  if (server) { server.kill('SIGTERM'); const stopped = await Promise.race([closed.then(() => true), new Promise(done => setTimeout(() => done(false), 2000))]); if (!stopped) { server.kill('SIGKILL'); await closed; } }
  await rm(folder, { recursive: true, force: true });
}
