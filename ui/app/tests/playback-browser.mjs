// Actual React controls with a deterministic, silent test host. No WebAudio/DSP.
import assert from 'node:assert/strict';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: 'chrome', args: ['--enable-unsafe-swiftshader'] });
const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
const errors = [];
page.on('pageerror', error => errors.push(error.message));
await page.addInitScript(() => {
  const s = window.playbackTest = { calls: [], generation: 0, enabled: false, fail: null, deferred: [], deferPrepare: false, deferPlay: false };
  s.transport = { preparationId: null, generation: '0', state: 'stopped', positionUs: '0', durationUs: '0' };
  const audio = () => ({ status: s.enabled ? 'running' : 'disabled', deviceName: 'Silent test driver', masterGainDb: -18, peakDbfs: null, meterIsSimulated: false, clipping: false, activeSynthVoices: null, maxSynthVoices: 8, error: null });
  const session = { id: 'test', name: 'Playback fixture', state: 'frozen', origin: 'archive_snapshot', eventCount: 0, captureTruncated: false };
  s.release = () => { s.deferred.splice(0).forEach(resolve => resolve()); };
  window.atlasHost = { request: async request => {
    const { method, args } = request;
    s.calls.push({ method, args });
    if (s.fail === method) { s.fail = null; return { requestId: request.requestId, ok: false, error: { code: 'unavailable', message: 'Injected playback failure' } }; }
    let result;
    if (method === 'getCapabilities') result = { protocolVersion: 1, implementation: 'native', supportedMethods: ['getCapabilities', 'getChanges', 'getCatalog', 'listSessions', 'getMaterialEvents', 'prepareReplay', 'getTransport', 'setTransport', 'getAudioState', 'setAudioEnabled'], audioAvailable: true, maxEventBatch: 500, maxVisibleMarkers: 400 };
    if (method === 'getChanges') result = { storeId: 'fixture', nextSequence: '0', events: [] };
    if (method === 'getCatalog') result = { species: [], stations: [], sources: [] };
    if (method === 'listSessions') result = [session];
    if (method === 'getMaterialEvents') result = { total: 0, events: [] };
    if (method === 'getAudioState') result = audio();
    if (method === 'setAudioEnabled') { s.enabled = args.enabled; result = audio(); }
    if (method === 'getTransport') result = { ...s.transport };
    if (method === 'prepareReplay') {
      const generation = String(++s.generation);
      const prepared = { preparationId: `prep-${generation}`, generation, durationUs: '6500000', eventCount: 0, peakEventsPerSecond: 0 };
      s.transport = { ...prepared, state: 'stopped', positionUs: '0' };
      s.enabled = false;
      if (s.deferPrepare) { s.deferPrepare = false; await new Promise(resolve => s.deferred.push(resolve)); }
      result = prepared;
    }
    if (method === 'setTransport') {
      if (args.action === 'play' && s.deferPlay) { s.deferPlay = false; const old = { ...s.transport, state: 'playing' }; await new Promise(resolve => s.deferred.push(resolve)); return { requestId: request.requestId, ok: true, result: old }; }
      if (args.action === 'play') s.transport.state = 'playing';
      if (args.action === 'pause') s.transport.state = 'paused';
      if (args.action === 'stop' || args.action === 'panic') { s.transport.state = 'stopped'; s.transport.positionUs = '0'; }
      if (args.action === 'seek') s.transport.positionUs = args.positionUs;
      result = { ...s.transport };
    }
    return { requestId: request.requestId, ok: true, result };
  } };
});
const wait = fn => page.waitForFunction(fn);
const button = name => page.getByRole('button', { name, exact: true });
const radio = name => page.getByRole('radio', { name, exact: true });
const listening = () => page.getByRole('switch', { name: /Listening/ });
const select = async () => { await button('Playback fixture 0 records').click(); await wait(() => window.playbackTest.transport.preparationId !== null); await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor(); };
try {
  await page.goto(process.env.ATLAS_TEST_URL || 'http://127.0.0.1:5173');
  await radio('Replay').click();
  await select();
  assert.equal(await button('Play').isDisabled(), true);
  await listening().click(); await wait(() => window.playbackTest.enabled);
  await button('Play').click(); await button('Pause playback').waitFor();
  assert.deepEqual(await page.evaluate(() => window.playbackTest.calls.filter(c => c.method === 'setTransport' && c.args.action === 'play').at(-1).args), { action: 'play', preparationId: 'prep-1', generation: '1' });
  await page.evaluate(() => { window.playbackTest.transport.positionUs = '3500000'; });
  await wait(() => document.querySelector('.header-clock').textContent.includes('0:03'));
  await button('Pause playback').click(); await button('Play').waitFor();
  await button('Return to start').click();
  await wait(() => window.playbackTest.transport.positionUs === '0');
  await page.evaluate(() => { window.playbackTest.fail = 'setTransport'; });
  await button('Return to start').click(); await page.getByRole('alert').filter({ hasText: 'Injected playback failure' }).waitFor();
  await page.evaluate(() => { window.playbackTest.fail = 'setAudioEnabled'; });
  await listening().click(); await page.getByRole('alert').filter({ hasText: 'Injected playback failure' }).waitFor();
  await radio('10×').click();
  await wait(() => window.playbackTest.generation === 2 && !window.playbackTest.enabled);
  await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor();
  assert.equal(await button('Play').isDisabled(), true);
  await listening().click(); await button('Play').click(); await button('Pause playback').waitFor();
  const prepared = await page.evaluate(() => window.playbackTest.generation);
  await button('Sound Studio').click(); await button('Atlas').click();
  assert.equal(await page.evaluate(() => window.playbackTest.generation), prepared);
  await radio('Live').click(); await wait(() => !window.playbackTest.enabled && window.playbackTest.transport.state === 'stopped');
  assert.equal(await page.getByRole('button', { name: 'Play (not available for live arrivals)' }).isDisabled(), true);
  await radio('Replay').click(); await select();
  // A delayed play ack from A cannot attach to B after a context change.
  await listening().click(); await page.evaluate(() => { window.playbackTest.deferPlay = true; });
  await button('Play').click();
  await wait(() => window.playbackTest.deferred.length === 1);
  await radio('60×').click();
  await wait(() => window.playbackTest.generation >= 5 && !window.playbackTest.enabled);
  await page.evaluate(() => window.playbackTest.release());
  await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor();
  assert.equal(await button('Play').isDisabled(), true);
  assert.equal(await button('Pause playback').count(), 0);
  // Superseded preparation replies and old push generations cannot reinstate A.
  await page.evaluate(() => { window.playbackTest.deferPrepare = true; });
  await radio('10×').click(); await wait(() => window.playbackTest.deferred.length === 1);
  await radio('240×').click(); await wait(() => window.playbackTest.generation >= 7);
  await page.evaluate(() => window.playbackTest.release());
  await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor();
  await listening().click(); await button('Play').click(); await button('Pause playback').waitFor();
  assert.equal(await page.evaluate(() => window.playbackTest.calls.filter(c => c.method === 'setTransport' && c.args.action === 'play').at(-1).args.generation), await page.evaluate(() => String(window.playbackTest.generation)));
  assert.equal(await page.getByRole('slider', { name: 'Master level' }).isDisabled(), true);
  assert.deepEqual(errors, []);
  console.log(JSON.stringify({ checks: ['explicit enable and generation-fenced play', 'native polled position including tail duration', 'pause and acknowledged return-to-start', 'visible seek/audio errors', 'speed/mode changes stop and disable', 'screen continuity', 'late play/preparation fencing', 'unsupported master disabled'], pageErrors: errors }));
} finally { await browser.close(); }
