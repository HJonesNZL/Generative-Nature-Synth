// Native-capability UI fixture only. No provider, database, collector or audio.
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const origin = process.env.ATLAS_TEST_URL || 'http://127.0.0.1:5173';
const output = resolve(process.env.ATLAS_LIVE_SOURCES_EVIDENCE || 'test-results/live-sources');
const browser = await chromium.launch({ headless: true, channel: 'chrome', args: ['--enable-unsafe-swiftshader'] });
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
const errors = [], external = [], bridge = [];
let navigations = 0;
page.on('pageerror', error => errors.push(error.message));
page.on('framenavigated', frame => { if (frame === page.mainFrame()) navigations++; });
page.on('request', request => {
  if (/^https?:/.test(request.url()) && new URL(request.url()).origin !== new URL(origin).origin) external.push(request.url());
  if (request.url().includes('/__atlas/')) bridge.push(request.url());
});
page.setDefaultTimeout(15000);
await page.addInitScript(() => {
  const saved = JSON.parse(sessionStorage.getItem('liveSourceFixture') || 'null');
  const paused = id => ({ id, enabled: false, polling: false, lastAttemptUtc: null, lastSuccessUtc: null, lastObservationUtc: null, error: null, records: '0', complete: false, gap: false, nextPollSeconds: 0 });
  const fixture = window.liveSourceTest = { state: saved?.state || { collecting: false, libraryActive: false, sources: [paused('birdweather'), paused('nzsharks')] }, calls: saved?.calls || [], failApply: false, failRead: false, libraryActive: saved?.libraryActive || false };
  const persist = () => sessionStorage.setItem('liveSourceFixture', JSON.stringify({ state: fixture.state, calls: fixture.calls, libraryActive: fixture.libraryActive }));
  const methods = ['getCapabilities', 'getLiveSources', 'setLiveSources', 'getCatalog', 'getChanges'];
  window.atlasHost = { request: async request => {
    const { method, args } = request;
    fixture.calls.push({ method, args: structuredClone(args) });
    let result;
    if (method === 'getCapabilities') result = { protocolVersion: 1, implementation: 'native', supportedMethods: methods, audioAvailable: false, libraryAvailable: fixture.libraryActive, maxEventBatch: 500, maxVisibleMarkers: 400 };
    else if (method === 'getCatalog') result = { species: [], stations: [], sources: [], limited: false };
    else if (method === 'getChanges') result = { storeId: 'synthetic-managed-live', nextSequence: '0', events: [] };
    else if (method === 'getLiveSources') {
      if (fixture.failRead) return { requestId: request.requestId, ok: false, error: { code: 'unavailable', message: 'Synthetic live status unavailable' } };
      result = fixture.state;
    } else if (method === 'setLiveSources') {
      if (fixture.failApply) { fixture.failApply = false; return { requestId: request.requestId, ok: false, error: { code: 'unavailable', message: 'Synthetic source selection failed' } }; }
      const libraryChanged = !fixture.libraryActive && args.sourceIds.length > 0;
      fixture.libraryActive ||= args.sourceIds.length > 0;
      fixture.state.libraryActive = fixture.libraryActive;
      fixture.state.collecting = args.sourceIds.length > 0;
      fixture.state.sources = fixture.state.sources.map(source => ({ ...source, enabled: args.sourceIds.includes(source.id), polling: false, nextPollSeconds: args.sourceIds.includes(source.id) ? source.id === 'birdweather' ? 60 : 900 : 0 }));
      persist();
      result = { ...fixture.state, libraryChanged };
    } else throw Error(`Unexpected live-source fixture method: ${method}`);
    return { requestId: request.requestId, ok: true, result: structuredClone(result) };
  } };
});
const button = name => page.getByRole('button', { name, exact: true });
const sourceSection = page.getByRole('region', { name: 'Live source status', exact: true });
const menu = () => page.getByRole('dialog', { name: /live collection/i });
const open = () => page.getByRole('button', { name: /^(Start live collection|Live collection options)$/ }).click();
const settle = () => page.evaluate(async () => { await document.fonts.ready; for (let frame = 0; frame < 4; ++frame) await new Promise(resolve => requestAnimationFrame(resolve)); });
try {
  await mkdir(output, { recursive: true });
  await page.goto(origin);
  await page.getByText('No live library connected.', { exact: false }).waitFor();
  await sourceSection.getByText('Updates paused', { exact: true }).first().waitFor();
  assert.equal(await sourceSection.getByText('Connected', { exact: true }).count(), 0);
  await open();
  await menu().getByText('Updates run only while this app is open.', { exact: false }).waitFor();
  assert.equal(await menu().getByText('Play while collecting', { exact: true }).count(), 0);
  assert.equal(await menu().getByRole('group', { name: 'Duration', exact: true }).count(), 0);
  await menu().getByLabel('NZ shark reports', { exact: true }).uncheck();
  await page.keyboard.press('Escape');
  await open();
  assert.equal(await menu().getByLabel('NZ shark reports', { exact: true }).isChecked(), false, 'Source draft survives disclosure close');
  await menu().getByText('Source scope and timing', { exact: true }).click();
  await menu().getByText('identified individuals and continuous movement are not guaranteed.', { exact: false }).waitFor();
  await page.evaluate(() => { window.liveSourceTest.failApply = true; });
  await menu().getByRole('button', { name: 'Start live updates', exact: true }).click();
  await menu().getByRole('alert').filter({ hasText: 'Synthetic source selection failed' }).waitFor();
  assert.equal(await menu().getByLabel('NZ shark reports', { exact: true }).isChecked(), false);
  const navigationsBeforeStart = navigations;
  await menu().getByRole('button', { name: 'Start live updates', exact: true }).click();
  await page.waitForFunction(() => window.liveSourceTest.libraryActive && window.liveSourceTest.state.collecting);
  await sourceSection.getByText('Waiting for first update', { exact: true }).waitFor();
  assert.ok(navigations > navigationsBeforeStart, 'Managed-library activation reloads the React app');
  assert.equal(await page.getByText('No live library connected.', { exact: false }).count(), 0);
  assert.deepEqual(await page.evaluate(() => window.liveSourceTest.calls.filter(call => call.method === 'setLiveSources').at(-1).args), { sourceIds: ['birdweather'] });
  await page.evaluate(() => { window.liveSourceTest.state.sources[0].polling = true; });
  await sourceSection.getByText('Updating…', { exact: true }).waitFor();
  await page.evaluate(() => {
    Object.assign(window.liveSourceTest.state.sources[0], { polling: false, lastAttemptUtc: new Date(Date.now() - 60000).toISOString(), lastSuccessUtc: new Date(Date.now() - 60000).toISOString(), lastObservationUtc: new Date(Date.now() - 3 * 86400000).toISOString(), records: '123', complete: false, gap: true });
  });
  await sourceSection.getByText('Last update 1 min ago', { exact: true }).waitFor();
  await sourceSection.getByText('Newest detection 3 d old', { exact: true }).waitFor();
  await sourceSection.getByText('Collection gap reported', { exact: true }).waitFor();
  await sourceSection.getByText('Partial source update · coverage incomplete', { exact: true }).waitFor();
  await page.evaluate(() => { window.liveSourceTest.state.sources[0].error = 'pagination_limit_reached'; });
  await sourceSection.getByText('Partial update · coverage incomplete', { exact: true }).waitFor();
  await sourceSection.getByText('Source page limit reached.', { exact: true }).waitFor();
  assert.equal(await sourceSection.getByText('Update failed · retry scheduled', { exact: true }).count(), 0);
  await page.evaluate(() => { window.liveSourceTest.state.sources[0].error = 'invalid_or_future_records'; });
  await sourceSection.getByText('Invalid or future-dated reports excluded.', { exact: true }).waitFor();
  await page.evaluate(() => { window.liveSourceTest.state.sources[0].error = 'rate_limited'; window.liveSourceTest.state.sources[0].nextPollSeconds = 86400; });
  await sourceSection.getByText('Update failed · retry scheduled', { exact: true }).waitFor();
  await sourceSection.getByText('Source error: rate limited', { exact: true }).waitFor();
  await open();
  await menu().getByLabel('NZ shark reports', { exact: true }).check();
  await menu().getByRole('button', { name: 'Apply source selection', exact: true }).click();
  await page.waitForFunction(() => window.liveSourceTest.state.sources.every(source => source.enabled));
  assert.deepEqual(await page.evaluate(() => window.liveSourceTest.calls.filter(call => call.method === 'setLiveSources').at(-1).args), { sourceIds: ['birdweather', 'nzsharks'] });
  await open();
  await menu().getByRole('button', { name: 'Pause updates', exact: true }).click();
  await page.getByRole('button', { name: 'Start live collection', exact: true }).waitFor();
  await sourceSection.getByText('Live updates paused. Open Collect to start again.', { exact: true }).waitFor();
  await page.evaluate(() => { window.liveSourceTest.failRead = true; });
  await sourceSection.getByRole('alert').filter({ hasText: 'Synthetic live status unavailable' }).waitFor();
  await page.evaluate(() => { window.liveSourceTest.failRead = false; });
  await sourceSection.getByRole('button', { name: 'Retry', exact: true }).click();
  await sourceSection.getByRole('alert').waitFor({ state: 'hidden' });
  await page.evaluate(() => { window.liveSourceTest.state.libraryActive = false; });
  await sourceSection.getByText('Library browsing only. Live updates paused. Open Collect to choose sources.', { exact: true }).waitFor();
  assert.equal(await page.getByText('No live library connected.', { exact: false }).count(), 0, 'An external library is distinguished from scene-only mode');
  await page.evaluate(() => { window.liveSourceTest.state.libraryActive = true; });
  await sourceSection.getByText('Live updates paused. Open Collect to start again.', { exact: true }).waitFor();
  for (const size of [{ width: 1440, height: 900 }, { width: 1280, height: 800 }]) {
    await page.setViewportSize(size); await settle(); await page.locator('.map-stage canvas').screenshot();
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
    await page.screenshot({ path: resolve(output, `atlas-${size.width}x${size.height}.png`) });
    await open();
    if (await menu().getByRole('button', { name: 'Retry', exact: true }).count()) {
      await menu().getByRole('button', { name: 'Retry', exact: true }).click();
      await menu().getByRole('alert').waitFor({ state: 'hidden' });
    }
    await menu().getByText('Source scope and timing', { exact: true }).click();
    await settle();
    const dialog = await menu().boundingBox();
    assert.ok(dialog.x >= 0 && dialog.x + dialog.width <= size.width && dialog.y + dialog.height <= size.height, 'Disclosure fits supported viewport');
    await page.screenshot({ path: resolve(output, `collect-${size.width}x${size.height}.png`) });
    await page.keyboard.press('Escape');
    await button('Sound Studio').click(); await settle();
    await page.screenshot({ path: resolve(output, `studio-${size.width}x${size.height}.png`) });
    await button('Atlas').click(); await settle();
  }
  const calls = await page.evaluate(() => window.liveSourceTest.calls);
  assert.equal(calls.some(call => ['getCollectionState', 'setCollection', 'getSourceHealth', 'setAudioEnabled'].includes(call.method)), false);
  assert.deepEqual(errors, []); assert.deepEqual(external, []); assert.deepEqual(bridge, []);
  const result = { checks: ['explicit native source selection with disclosure draft retention', 'no legacy duration/listen promises or legacy collection calls', 'failed apply observable and retry preserves draft', 'managed-library activation reload', 'enabled does not imply Connected', 'waiting/updating/last update/actual observation age separate', 'partial coverage and collection gap visible', 'page-cap and excluded-record results are partial updates, not failed fetches', 'rate-limited source and 86400s retry supported', 'apply both source streams, pause updates and status read recovery', 'external archive browsing distinguished from scene-only startup', '1440x900 and 1280x800 Atlas/Collect/Studio layouts'], pageErrors: errors, external, bridge };
  await writeFile(resolve(output, 'result.json'), JSON.stringify(result, null, 2));
  console.log(JSON.stringify(result));
} finally { await browser.close(); }
