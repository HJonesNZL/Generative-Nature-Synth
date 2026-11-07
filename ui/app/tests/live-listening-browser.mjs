// Isolated injected host. No network provider, database or audio device.
import assert from 'node:assert/strict';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: 'chrome', args: ['--enable-unsafe-swiftshader'] });
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
await page.addInitScript(() => {
  const state = window.liveListeningFixture = { enabled: false, status: 'off', received: 0, admitted: 0, dropped: 0, skippedRevisions: 0, error: '', calls: [], fail: false };
  const methods = ['getCapabilities','getCatalog','getChanges','getLiveState','setLiveEnabled','getAudioState'];
  window.atlasHost = { subscribe: listener => { window.liveConnection = listener; return () => {}; }, request: async request => {
    const { method, args } = request;
    state.calls.push({ method, args });
    let result;
    if (method === 'getCapabilities') result = { protocolVersion: 1, implementation: 'native', supportedMethods: methods, audioAvailable: true, libraryAvailable: true, maxEventBatch: 500, maxVisibleMarkers: 400 };
    else if (method === 'getCatalog') result = { species: [], stations: [], sources: [], limited: false };
    else if (method === 'getChanges') result = { storeId: 'isolated-live', nextSequence: '0', events: [] };
    else if (method === 'getAudioState') result = { status: state.enabled ? 'running' : 'disabled', deviceName: null, masterGainDb: -18, peakDbfs: null, meterIsSimulated: false, clipping: false, activeSynthVoices: 0, maxSynthVoices: 24 };
    else {
      if (method === 'setLiveEnabled') {
        if (state.fail) return { ok: false, requestId: request.requestId, error: { code: 'unavailable', message: 'Synthetic live start failed' } };
        await new Promise(resolve => setTimeout(resolve, 100));
        state.enabled = args.enabled; state.status = args.enabled ? 'listening' : 'off';
      }
      result = { enabled: state.enabled, status: state.status, received: state.received, admitted: state.admitted, dropped: state.dropped, skippedRevisions: state.skippedRevisions, error: state.error };
    }
    return { ok: true, requestId: request.requestId, result };
  } };
});
try {
  await page.goto(process.env.ATLAS_TEST_URL || 'http://127.0.0.1:5173');
  const toggle = () => page.getByRole('switch', { name: /Live listening/ });
  await toggle().waitFor();
  assert.equal(await toggle().getAttribute('aria-checked'), 'false');
  assert.equal(await page.evaluate(() => liveListeningFixture.calls.filter(c => c.method === 'setLiveEnabled').length), 0);
  await toggle().click();
  await page.getByRole('switch', { name: 'Live listening (on)', exact: true }).waitFor();
  await page.getByRole('radio', { name: 'Live', exact: true }).click();
  assert.equal(await page.evaluate(() => liveListeningFixture.calls.filter(c => c.method === 'setLiveEnabled').length), 1, 'reselecting Live preserves listening');
  await page.getByRole('button', { name: 'Sound Studio', exact: true }).click();
  await page.getByRole('switch', { name: 'Live listening (on)', exact: true }).waitFor();
  assert.equal(await page.evaluate(() => liveListeningFixture.calls.filter(c => c.method === 'setLiveEnabled').length), 1);
  await page.evaluate(() => liveConnection({ type: 'connection', state: 'disconnected' }));
  await page.getByText('Live unknown', {exact:true}).waitFor();
  await page.evaluate(() => liveConnection({ type: 'connection', state: 'connected' }));
  await page.getByRole('switch', { name: 'Live listening (on)', exact: true }).waitFor();
  assert.equal(await page.evaluate(() => liveListeningFixture.calls.filter(c => c.method === 'setLiveEnabled').length), 1, 'reconnect never enables automatically');
  await page.evaluate(() => { liveListeningFixture.dropped = 3; liveListeningFixture.skippedRevisions = 7; });
  await page.getByRole('status').filter({ hasText: 'Live overload' }).waitFor();
  await page.getByRole('radio', { name: 'Replay', exact: true }).click();
  await page.waitForFunction(() => !liveListeningFixture.enabled);
  assert.deepEqual(await page.evaluate(() => liveListeningFixture.calls.filter(c => c.method === 'setLiveEnabled').map(c => c.args)), [{enabled:true},{enabled:false}]);
  await page.getByRole('radio', { name: 'Live', exact: true }).click();
  await page.evaluate(() => { liveListeningFixture.fail = true; });
  await toggle().click();
  await page.getByRole('alert').filter({hasText:'Synthetic live start failed'}).waitFor();
  assert.equal(await toggle().getAttribute('aria-checked'), 'false');
  for (const viewport of [{width:1440,height:900},{width:1280,height:800}]) {
    await page.setViewportSize(viewport);
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
  }
  console.log('Live listening browser checks passed: opt-in, persistent across screens, overload, mode disable, failure, desktop widths.');
} finally { await browser.close(); }
