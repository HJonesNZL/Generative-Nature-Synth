// Silent browser fixture: observable React device selection/recovery, no hardware output.
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: process.env.ATLAS_BROWSER || 'chrome', args: ['--enable-unsafe-swiftshader'] });
const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
const errors = [], requests = [];
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => requests.push(request.url()));
await page.addInitScript(() => {
  const s = window.deviceTest = { calls: [], supported: true, enabled: false, revision: 1, fail: false, lost: false, missing: false, deferCatalog: false, releaseCatalog: null };
  const output = id => ({ id, name: id === 'a' ? 'Studio output' : 'USB output', type: 'Silent test', isDefault: id === 'a', outputChannels: 2, sampleRates: [44100, 48000], bufferFrames: [128, 256, 512], defaultBufferFrames: 256 });
  s.configuration = { mode: 'systemDefault', deviceId: null, bufferFrames: 0 };
  s.transport = { preparationId: null, generation: '0', state: 'stopped', positionUs: '0', durationUs: '0' };
  const name = () => s.configuration.deviceId === 'b' ? 'USB output' : 'Studio output';
  const audio = () => ({ status: s.lost ? 'device_lost' : s.enabled ? 'running' : 'disabled', deviceName: s.enabled ? name() : null, masterGainDb: -18, peakDbfs: null, meterIsSimulated: false, clipping: false, activeSynthVoices: 0, maxSynthVoices: 8, error: s.lost ? 'Output disconnected' : null });
  const catalog = () => ({ available: true, devices: s.missing ? [] : [output('a'), output('b')], configuration: { ...s.configuration }, active: s.enabled ? { deviceId: s.configuration.deviceId || 'a', name: name(), type: 'Silent test', sampleRate: 48000, bufferFrames: s.configuration.bufferFrames || 256, outputChannels: 2 } : null, controlRevision: String(s.revision), preparationId: s.transport.preparationId, generation: s.transport.generation, error: s.lost ? 'Output disconnected' : null });
  const session = { id: 'test', name: 'Device test piece', state: 'frozen', origin: 'archive_snapshot', eventCount: 0, captureTruncated: false };
  window.atlasHost = { request: async request => {
    const { method, args } = request; s.calls.push({ method, args });
    let result;
    if (method === 'getCapabilities') result = { protocolVersion: 1, implementation: 'native', supportedMethods: ['getCapabilities', 'getChanges', 'getCatalog', 'listSessions', 'getMaterialEvents', 'prepareReplay', 'getTransport', 'setTransport', 'getAudioState', 'setAudioEnabled', ...(s.supported ? ['getAudioDevices', 'setAudioDevice'] : [])], audioAvailable: true, maxEventBatch: 500, maxVisibleMarkers: 500 };
    if (method === 'getChanges') result = { storeId: 'fixture', nextSequence: '0', events: [] };
    if (method === 'getCatalog') result = { species: [], stations: [], sources: [] };
    if (method === 'listSessions') result = [session];
    if (method === 'getMaterialEvents') result = { total: 0, events: [] };
    if (method === 'getAudioState') result = audio();
    if (method === 'getAudioDevices') { if (s.deferCatalog) { s.deferCatalog = false; await new Promise(resolve => { s.releaseCatalog = resolve; }); } result = catalog(); }
    if (method === 'setAudioEnabled') { s.enabled = args.enabled; s.lost = false; s.transport.generation = String(Number(s.transport.generation) + 1); result = audio(); }
    if (method === 'getTransport') result = { ...s.transport };
    if (method === 'prepareReplay') {
      s.transport = { preparationId: 'prepared', generation: String(Number(s.transport.generation) + 1), state: 'stopped', positionUs: '0', durationUs: '5000000' };
      result = { ...s.transport, eventCount: 0, peakEventsPerSecond: 0 };
    }
    if (method === 'setTransport') { s.transport.state = args.action === 'play' ? 'playing' : 'stopped'; result = { ...s.transport }; }
    if (method === 'setAudioDevice') {
      if (s.fail) { s.fail = false; return { requestId: request.requestId, ok: false, error: { code: 'unavailable', message: 'Injected output open failure' } }; }
      if (args.controlRevision !== String(s.revision) || args.generation !== s.transport.generation || args.preparationId !== s.transport.preparationId) return { requestId: request.requestId, ok: false, error: { code: 'stale_generation', message: 'Output selection changed' } };
      s.configuration = { ...args.configuration }; s.enabled = false; s.lost = false; s.revision += 1;
      s.transport.generation = String(Number(s.transport.generation) + 1); s.transport.state = 'stopped'; s.transport.positionUs = '0';
      result = catalog();
    }
    return { requestId: request.requestId, ok: true, result };
  } };
});
const button = name => page.getByRole('button', { name, exact: true });
const wait = predicate => page.waitForFunction(predicate);
const url = process.env.ATLAS_TEST_URL || 'http://127.0.0.1:5173/';
try {
  await page.goto(url);
  await button('Audio settings').click();
  await page.getByRole('combobox', { name: 'Output device', exact: true }).selectOption('device:b');
  await page.getByRole('combobox', { name: 'Buffer size', exact: true }).selectOption('128');
  assert.equal(await page.evaluate(() => window.deviceTest.calls.filter(call => call.method === 'setAudioDevice').length), 0);
  await button('Apply output').click();
  await page.getByRole('status').filter({ hasText: 'Output applied. Listening is off.' }).waitFor();
  assert.deepEqual(await page.evaluate(() => window.deviceTest.calls.filter(call => call.method === 'setAudioDevice').at(-1).args.configuration), { mode: 'explicitDevice', deviceId: 'b', bufferFrames: 128 });
  assert.equal(await page.evaluate(() => window.deviceTest.enabled), false);
  await page.keyboard.press('Escape');
  await page.getByRole('radio', { name: 'Replay', exact: true }).click();
  await button('Device test piece 0 records').click();
  await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor();
  await page.getByRole('switch', { name: /Listening/ }).click();
  await button('Play').click(); await button('Pause playback').waitFor();
  await button('Audio settings').click();
  await page.getByText('USB output · 48,000 Hz · 128 frames · 2 outputs', { exact: true }).waitFor();
  const before = await page.evaluate(() => window.deviceTest.calls.length);
  await page.getByRole('combobox', { name: 'Buffer size', exact: true }).selectOption('512');
  await button('Apply output').click();
  await page.getByRole('status').filter({ hasText: 'Output applied. Listening is off.' }).waitFor();
  assert.equal(await button('Play').isDisabled(), true);
  assert.equal(await page.getByRole('switch', { name: /Listening/ }).isChecked(), false);
  assert.equal(await page.evaluate(() => window.deviceTest.transport.state), 'stopped');
  assert.equal(await page.evaluate(before => window.deviceTest.calls.slice(before).some(call => call.method === 'setAudioEnabled' && call.args.enabled || call.method === 'setTransport' && call.args.action === 'play'), before), false);
  await page.evaluate(() => { window.deviceTest.deferCatalog = true; });
  await button('Refresh devices').click();
  await button('Refreshing…').waitFor();
  assert.equal(await page.getByRole('combobox', { name: 'Output device', exact: true }).isDisabled(), true);
  assert.equal(await page.getByRole('combobox', { name: 'Buffer size', exact: true }).isDisabled(), true);
  assert.equal(await button('Apply output').isDisabled(), true);
  await page.waitForFunction(() => window.deviceTest.releaseCatalog !== null);
  await page.evaluate(() => { window.deviceTest.releaseCatalog(); window.deviceTest.releaseCatalog = null; });
  await button('Refresh devices').waitFor();
  await page.evaluate(() => { window.deviceTest.revision += 1; });
  await button('Apply output').click();
  await page.getByRole('status').filter({ hasText: 'Refresh devices, then apply again.' }).waitFor();
  await button('Refresh devices').click(); await button('Apply output').click();
  await page.getByRole('status').filter({ hasText: 'Output applied. Listening is off.' }).waitFor();
  await page.evaluate(() => { window.deviceTest.fail = true; });
  await button('Apply output').click();
  await page.getByRole('status').filter({ hasText: 'Injected output open failure' }).waitFor();
  await page.evaluate(() => { window.deviceTest.lost = true; window.deviceTest.missing = true; });
  await button('Refresh devices').click();
  await page.getByText('No output devices found. Connect an output and refresh.', { exact: true }).waitFor();
  assert.equal(await button('Apply output').isDisabled(), true);
  await page.getByRole('combobox', { name: 'Output device', exact: true }).selectOption('default');
  assert.equal(await button('Apply output').isDisabled(), true);
  await page.evaluate(() => { window.deviceTest.missing = false; });
  await button('Refresh devices').click();
  await page.getByRole('combobox', { name: 'Output device', exact: true }).selectOption('default');
  await button('Apply output').click();
  await page.getByRole('status').filter({ hasText: 'Output applied. Listening is off.' }).waitFor();
  await page.evaluate(() => document.fonts.ready);
  await mkdir('test-results', { recursive: true });
  await page.screenshot({ path: 'test-results/audio-device-preferences.png' });
  await page.keyboard.press('Escape');
  await page.getByRole('switch', { name: /Listening/ }).click();
  await button('Play').click(); await button('Pause playback').waitFor();
  const play = await page.evaluate(() => window.deviceTest.calls.filter(call => call.method === 'setTransport' && call.args.action === 'play').at(-1).args);
  assert.equal(play.generation, await page.evaluate(() => window.deviceTest.transport.generation));
  assert.deepEqual(errors, []);
  assert.ok(await page.evaluate(() => window.deviceTest.calls.some(call => call.method === 'getChanges')), 'An older host without libraryAvailable retains its feed behavior');
  assert.ok(requests.every(request => request.startsWith(new URL(url).origin) || request.startsWith('data:')));
  const gated = await browser.newPage();
  await gated.addInitScript(() => { window.atlasHost = { request: async request => ({ requestId: request.requestId, ok: true, result: { protocolVersion: 1, implementation: 'native', supportedMethods: ['getCapabilities'], audioAvailable: false, maxEventBatch: 500, maxVisibleMarkers: 500 } }) }; });
  await gated.goto(url); await gated.getByLabel('Project', { exact: true }).click();
  await gated.locator('.project-tools__help summary').click();
  await gated.getByText('Audio output is unavailable in this host. Available data and export controls still work.', { exact: true }).waitFor();
  await gated.keyboard.press('Escape');
  assert.equal(await gated.getByRole('button', { name: 'Audio settings', exact: true }).count(), 0);
  await gated.close();
  const result = { origin: url, hostTransport: "injected silent fixture", browser: browser.version(), errors, checks: ['capability gate', 'draft selection stays local until Apply', 'explicit device and buffer fence', 'active output rate and buffer', 'applying during playback stops and disables without autoplay', 'catalog refresh blocks draft edits until complete', 'stale fence requires refresh', 'native opening failure is visible', 'disconnect with missing device and recovery', 'default output mode', 'manual Listening and Play use fresh generation', 'no external requests'], screenshot: 'test-results/audio-device-preferences.png' };
  await writeFile('test-results/audio-devices-browser.json', JSON.stringify(result, null, 2) + '\n');
  console.log(JSON.stringify(result));
} finally { await browser.close(); }
