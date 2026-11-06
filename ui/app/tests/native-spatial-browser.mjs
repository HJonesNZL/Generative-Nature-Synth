// User-authored regional mixing through real native host; silent output and disposable state only.
import assert from 'node:assert/strict';
import { spawn, execFileSync } from 'node:child_process';
import { mkdtemp, rm, mkdir } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, resolve, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const app = resolve(dirname(fileURLToPath(import.meta.url)), '..'), root = resolve(app, '../..');
const folder = await mkdtemp(join(tmpdir(), 'atlas-spatial-ui-'));
const binary = process.env.ATLAS_STUDIO_FIXTURE_BINARY || resolve(root, 'build/native/atlas-studio-fixture-host');
const port = Number(process.env.ATLAS_SPATIAL_TEST_PORT || 5195), origin = `http://127.0.0.1:${port}`;
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
let browser, server, closed;
const errors = [], rejected = [], replies = [], requests = [];
const button = (page, name) => page.getByRole('button', { name, exact: true });
const input = (page, name) => page.getByRole('spinbutton', { name, exact: true });
const deferred = () => { let resolve; const promise = new Promise(done => { resolve = done; }); return { resolve, promise }; };
let probeId = 0;
async function raw(page, method, args) {
  const response = await page.request.post(`${origin}/__atlas/request`, { headers: { origin }, data: { protocolVersion: 1, requestId: `spatial-probe-${++probeId}`, method, args } });
  assert.equal(response.status(), 200); return response.json();
}
async function start() {
  server = spawn(process.execPath, ['node_modules/vite/bin/vite.js', '--port', String(port), '--strictPort'], { cwd: app, env: { ...process.env,
    ATLAS_AUDIO_HOST_BINARY: '', ATLAS_NATIVE_BINARY: binary, ATLAS_LIBRARY: join(folder, 'synthetic.sqlite3'), ATLAS_VOICE_LIBRARY: join(folder, 'voices.sqlite3'),
    ATLAS_SOUNDSCAPE_DIRECTORY: join(folder, 'scenes'), ATLAS_EXPORT_DIRECTORY: join(folder, 'exports') }, stdio: ['pipe', 'pipe', 'pipe'] });
  closed = new Promise(done => server.once('exit', done)); server.stdout.resume(); server.stderr.resume();
  for (let attempt = 0; attempt < 100; attempt++) {
    if (server.exitCode !== null) throw Error('Isolated spatial UI server exited');
    try { if ((await fetch(origin)).ok) return; } catch {}
    await new Promise(done => setTimeout(done, 50));
  }
  throw Error('Isolated spatial UI server did not start');
}
async function exportSaved(page) {
  const previous = await page.locator('a[download]').count() ? await page.locator('a[download]').getAttribute('data-artifact-id') : null;
  await button(page, 'Export saved WAV').click();
  await page.waitForFunction(previous => { const link = document.querySelector('a[download]'); return link && link.getAttribute('data-artifact-id') !== previous; }, previous, { timeout: 30000 });
  const response = await page.request.get(new URL(await page.getByRole('link', { name: 'Download WAV', exact: true }).getAttribute('href'), origin).href);
  assert.equal(response.status(), 200); const bytes = await response.body(); assert.equal(bytes.subarray(0, 4).toString(), 'RIFF'); return bytes;
}
function stereoDifference(bytes) {
  let format, audio;
  for (let offset = 12; offset + 8 <= bytes.length;) {
    const kind = bytes.subarray(offset, offset + 4).toString(), size = bytes.readUInt32LE(offset + 4), data = offset + 8;
    if (kind === 'fmt ') format = { channels: bytes.readUInt16LE(data + 2), bits: bytes.readUInt16LE(data + 14) };
    if (kind === 'data') audio = bytes.subarray(data, data + size);
    offset = data + size + (size & 1);
  }
  assert.deepEqual(format, { channels: 2, bits: 24 }); assert.ok(audio);
  let different = 0; for (let i = 0; i + 5 < audio.length; i += 6) if (!audio.subarray(i, i + 3).equals(audio.subarray(i + 3, i + 6))) different++;
  return different;
}
try {
  execFileSync('python3', ['-B', 'tests/ui_fixture.py', join(folder, 'synthetic.sqlite3')], { cwd: root, stdio: 'pipe' });
  await mkdir(join(folder, 'scenes')); await start();
  browser = await chromium.launch({ headless: true, channel: 'chrome', args: ['--enable-unsafe-swiftshader'] });
  const page = await browser.newPage({ viewport: { width: 1700, height: 1200 } }); page.setDefaultTimeout(15000);
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => { if (request.url().includes('/__atlas/request')) requests.push(request.postDataJSON()); });
  page.on('response', async response => {
    if (!response.url().includes('/__atlas/request')) return;
    try { const request = response.request().postDataJSON(), reply = await response.json();
      if (reply.ok) replies.push({ method: request.method, result: reply.result }); else rejected.push({ method: request.method, error: reply.error });
    } catch {}
  });
  const gates = new Map();
  await page.route('**/__atlas/request', async route => {
    const gate = gates.get(route.request().postDataJSON().method); if (!gate) return route.continue();
    gates.delete(route.request().postDataJSON().method); const response = await route.fetch(); gate.result = (await response.json()).result; gate.seen.resolve(); await gate.ack.promise; await route.fulfill({ response });
  });
  await page.goto(origin); await page.getByRole('radio', { name: 'Replay', exact: true }).click();
  await page.getByRole('button', { name: /^Fixture • 2,005 records/ }).click();
  await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor();
  await page.getByRole('radio', { name: '240×', exact: true }).click(); await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor();
  await button(page, 'Sound Studio').click(); await page.getByRole('complementary', { name: 'Regional spatial mixing', exact: true }).waitFor();
  await page.getByRole('button', { name: /^Fixture Tūī species voice/ }).click();
  await page.getByRole('checkbox', { name: 'Enable composition', exact: true }).check(); await input(page, 'Composition tempo BPM').fill('117');
  await page.getByRole('checkbox', { name: 'Enable spatial mixing', exact: true }).check(); await input(page, 'Global ambience amount').fill('.7');
  await button(page, 'Add region').click(); await page.getByRole('textbox', { name: 'Region name', exact: true }).fill('Local geographic selection');
  for (const [name, value] of [['South latitude', '-40'], ['North latitude', '-37'], ['West longitude', '175'], ['East longitude', '178'], ['Pan', '-.7'], ['Gain dB', '-4'], ['Ambience send', '.8'], ['Echo / room blend', '.6'], ['Echo delay ms', '210'], ['Echo feedback', '.3'], ['Nominal room decay s', '1.5'], ['Room tone', '.4']]) await input(page, name).fill(value);
  await button(page, 'Add region').click(); await page.getByRole('textbox', { name: 'Region name', exact: true }).fill('Dateline geographic selection');
  await input(page, 'West longitude').fill('170'); await input(page, 'East longitude').fill('-170'); await input(page, 'Pan').fill('.6'); await input(page, 'Ambience send').fill('.4');
  await button(page, 'Move region earlier').click();
  await button(page, 'Add region').click(); await button(page, 'Remove region').click();
  assert.equal(await page.getByRole('list', { name: 'Region first-match order', exact: true }).getByRole('listitem').count(), 2);
  await page.getByRole('button', { name: 'Edit region 1: Dateline geographic selection', exact: true }).click();
  await button(page, 'Apply composition').click(); await page.getByText('Composition applied. Listening is off; enable it and press Play to hear this candidate.', { exact: true }).waitFor();
  assert.equal(requests.filter(r => r.method === 'setSpatial').length, 0, 'composition Apply must preserve the spatial draft');
  assert.equal(await input(page, 'Global ambience amount').inputValue(), '.7'); await page.getByText('Spatial edits pending', { exact: true }).waitFor();
  await input(page, 'Composition tempo BPM').fill('119');
  await button(page, 'Apply spatial settings').click(); await page.getByRole('status').filter({ hasText: 'Spatial settings applied. Listening is off; enable it and press Play to hear this candidate.' }).waitFor();
  assert.equal(await input(page, 'Composition tempo BPM').inputValue(), '119', 'spatial Apply preserves unsaved composition');
  assert.equal(requests.filter(r => r.method === 'setComposition').length, 1);
  const firstSpatial = replies.filter(r => r.method === 'setSpatial').at(-1).result;
  assert.ok(firstSpatial.routedNotes > 0); assert.equal(firstSpatial.unassignedNotes, 0); assert.equal(firstSpatial.wetActive, true);
  await input(page, 'Global ambience amount').fill('.55'); await input(page, 'Pan').fill(''); await input(page, 'Pan').pressSequentially('-.4'); assert.equal(await input(page, 'Pan').inputValue(), '-.4');
  await input(page, 'Echo feedback').fill('.6'); const priorSpatialRequests = requests.filter(r => r.method === 'setSpatial').length;
  await button(page, 'Apply spatial settings').click(); await page.getByText(/Echo feedback must be 0 to 0.5/).waitFor(); assert.equal(requests.filter(r => r.method === 'setSpatial').length, priorSpatialRequests);
  await input(page, 'Echo feedback').fill('.35'); await page.getByRole('checkbox', { name: 'Pan by reported longitude', exact: true }).check();
  await button(page, 'Atlas').click(); await button(page, 'Sound Studio').click(); assert.equal(await input(page, 'Global ambience amount').inputValue(), '.55'); assert.equal(await input(page, 'Composition tempo BPM').inputValue(), '119');
  await page.getByRole('textbox', { name: 'Soundscape name', exact: true }).fill('Ordered stereo spatial scene');
  const compositionGate = { seen: deferred(), ack: deferred() }, spatialGate = { seen: deferred(), ack: deferred() }; gates.set('setComposition', compositionGate); gates.set('setSpatial', spatialGate);
  await button(page, 'Save soundscape').click(); await compositionGate.seen.promise;
  assert.equal(replies.filter(r => r.method === 'saveSoundscape').length, 0); assert.equal(await button(page, 'Saving…').isDisabled(), true); assert.equal(requests.filter(r => r.method === 'setSpatial').length, priorSpatialRequests);
  compositionGate.ack.resolve(); await spatialGate.seen.promise;
  assert.equal(replies.filter(r => r.method === 'saveSoundscape').length, 0); assert.equal(await button(page, 'Saving…').isDisabled(), true);
  assert.equal(requests.filter(r => r.method === 'setSpatial').at(-1).args.preparationId, compositionGate.result.preparation.preparationId, 'second recipe must use the first acknowledged preparation');
  spatialGate.ack.resolve(); await page.getByText(/Saved “Ordered stereo spatial scene” with/).waitFor();
  const saved = replies.filter(r => r.method === 'saveSoundscape').at(-1).result, expectedSpatial = spatialGate.result.settings, expectedComposition = compositionGate.result.settings;
  assert.equal(expectedComposition.tempoBpm, 119); assert.equal(expectedSpatial.amount, .55); assert.deepEqual(expectedSpatial.regions.map(r => r.id), ['region-2', 'region-1']); assert.equal(expectedSpatial.regions[0].positionPan, true); assert.equal(expectedSpatial.regions[0].feedback, .35);
  const baseline = await exportSaved(page), differentStereoFrames = stereoDifference(baseline); assert.ok(differentStereoFrames > 1000, 'wet spatial WAV must contain real L/R differences');
  await input(page, 'Global ambience amount').fill('.1'); await button(page, 'Apply spatial settings').click(); await page.getByRole('status').filter({ hasText: 'Spatial settings applied. Listening is off; enable it and press Play to hear this candidate.' }).waitFor();
  await input(page, 'Composition tempo BPM').fill('130'); await button(page, 'Apply composition').click(); await page.getByText('Composition applied. Listening is off; enable it and press Play to hear this candidate.', { exact: true }).waitFor();
  assert.deepEqual(await exportSaved(page), baseline, 'export uses the saved immutable composition/spatial/voice recipe');
  const playCount = requests.filter(r => r.method === 'setTransport' && r.args.action === 'play').length;
  await button(page, 'Open soundscape').click(); await page.getByText(/Opened “Ordered stereo spatial scene”/).waitFor();
  await page.waitForFunction(() => document.querySelector('input[aria-label="Global ambience amount"]')?.value === '0.55');
  assert.equal(await input(page, 'Composition tempo BPM').inputValue(), '119'); assert.equal(await button(page, 'Play').isDisabled(), true); assert.equal(requests.filter(r => r.method === 'setTransport' && r.args.action === 'play').length, playCount);
  let spatial = (await raw(page, 'getSpatial', {})).result; assert.deepEqual(spatial.settings, expectedSpatial); assert.deepEqual((await raw(page, 'getComposition', {})).result.settings, expectedComposition);
  assert.deepEqual(await exportSaved(page), baseline, 'recalled immutable recipe renders the exact same WAV');
  const stale = await raw(page, 'setSpatial', { settings: expectedSpatial, preparationId: firstSpatial.preparation.preparationId, generation: firstSpatial.preparation.generation }); assert.equal(stale.error.code, 'stale_generation');
  for (const patch of [{ amount: 2 }, { enabled: 1 }, { regions: [...expectedSpatial.regions, expectedSpatial.regions[0]] }, { regions: [{ ...expectedSpatial.regions[0], west: 180, east: -180 }] }, { regions: [{ ...expectedSpatial.regions[0], feedback: .6 }] }]) {
    const result = await raw(page, 'setSpatial', { settings: { ...expectedSpatial, ...patch }, preparationId: spatial.preparation.preparationId, generation: spatial.preparation.generation }); assert.equal(result.error.code, 'invalid_request');
  }
  assert.equal((await raw(page, 'getSpatial', {})).result.preparation.preparationId, spatial.preparation.preparationId);
  await button(page, 'Atlas').click(); await page.getByRole('radio', { name: '60×', exact: true }).click(); await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor(); await button(page, 'Sound Studio').click();
  await page.waitForFunction(() => !document.querySelector('.native-spatial fieldset')?.disabled);
  spatial = (await raw(page, 'getSpatial', {})).result; assert.deepEqual(spatial.settings, expectedSpatial); assert.deepEqual((await raw(page, 'getComposition', {})).result.settings, expectedComposition);
  await page.getByRole('button', { name: /^Fixture Tūī species voice/ }).click();
  await page.getByRole('heading', { name: 'Fixture Tūī', exact: true }).waitFor();
  await page.locator('.native-spatial').evaluate(element => { element.scrollTop = 0; });
  const screenshot = process.env.ATLAS_SPATIAL_SCREENSHOT || '/private/tmp/atlas-spatial-ui.png';
  await page.screenshot({ path: screenshot, fullPage: true });
  await page.locator('.native-spatial').evaluate(element => { element.scrollTop = 450; });
  await page.screenshot({ path: screenshot.replace(/\.png$/, '-controls.png'), fullPage: true });
  // A different window changes the same source recipe. Fresh identity alone must not authorize overwrite.
  await input(page, 'Global ambience amount').fill('.9'); const beforeClash = requests.filter(r => r.method === 'setSpatial').length;
  await page.evaluate(async ({ origin, spatial }) => {
    const response = await fetch(`${origin}/__atlas/request`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ protocolVersion: 1, requestId: 'external-spatial-window', method: 'setSpatial', args: { settings: { ...spatial.settings, amount: .82 }, preparationId: spatial.preparation.preparationId, generation: spatial.preparation.generation } }) });
    const reply = await response.json(); if (!reply.ok) throw Error(reply.error.message);
    [...document.querySelectorAll('button')].find(button => button.textContent === 'Apply spatial settings').click();
  }, { origin, spatial });
  await page.getByText('The source piece or spatial recipe changed. Reload its settings before applying.', { exact: false }).waitFor(); assert.equal(requests.filter(r => r.method === 'setSpatial').length, beforeClash + 1);
  assert.equal((await raw(page, 'getSpatial', {})).result.settings.amount, .82);
  assert.deepEqual(errors, []); assert.deepEqual(rejected, []);
  console.log(JSON.stringify({ checks: ['temporary negative-decimal editing and invalid numeric rejection', 'two ordered user-authored bounds including dateline preview', 'composition Apply preserves pending spatial draft', 'spatial Apply preserves pending composition draft', 'combined Save waits for each independent acknowledgement and fresh preparation', 'complete composition and ordered spatial recipe exact recall', 'real stereo wet WAV with immutable export after later edits', 'quiet recall without autoplay', 'stale and malformed spatial requests leave preparation unchanged', 'composition and spatial settings survive rational retiming', 'same-piece external recipe change cannot be silently overwritten'], savedId: saved.id, wavBytes: baseline.length, differentStereoFrames, settings: expectedSpatial, composition: expectedComposition, pageErrors: errors, rejected }));
} catch (error) { console.error(JSON.stringify({ rejected, recent: replies.slice(-7), requests: requests.filter(r => ['setSpatial', 'setComposition', 'saveSoundscape'].includes(r.method)) })); throw error; }
finally {
  await browser?.close(); if (server) { server.kill('SIGTERM'); const stopped = await Promise.race([closed.then(() => true), new Promise(done => setTimeout(() => done(false), 2000))]); if (!stopped) { server.kill('SIGKILL'); await closed; } }
  await rm(folder, { recursive: true, force: true });
}
