// Silent injected host: tests partial-page invalidation without a library or audio.
import assert from 'node:assert/strict';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: 'chrome', args: ['--enable-unsafe-swiftshader'] });
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
const origin = process.env.ATLAS_TEST_URL || 'http://127.0.0.1:5173';
const errors = [], external = [], bridge = [];
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => {
  if (/^https?:/.test(request.url()) && new URL(request.url()).origin !== new URL(origin).origin) external.push(request.url());
  if (request.url().includes('/__atlas/')) bridge.push(request.url());
});
page.setDefaultTimeout(10000);
await page.addInitScript(() => {
  const fixture = window.materialRevisionTest = { calls: [], fail: true, deferred: [], prepares: 0 };
  const stamp = '2026-01-01T00:00:00.000000Z';
  const session = { id: 'revision-piece', name: 'Revision fixture', state: 'frozen', origin: 'archive_snapshot', eventCount: 2, captureTruncated: false };
  const event = index => ({ id: `revision-event-${index}`, revisionSequence: '1', sourceId: 'synthetic-revision', speciesId: 'synthetic-bird', commonName: 'Synthetic bird', individualId: null, deploymentId: null, stationId: 'synthetic-receiver', eventKind: 'acoustic_species_detection', locationKind: 'station_detection', position: { latitude: -40, longitude: 175, uncertaintyMetres: null }, observedAtUtc: stamp, firstSeenAtUtc: stamp, confidence: null, attribution: 'Synthetic fixture', qualityFlags: [], provenance: 'demo' });
  const methods = ['getCapabilities', 'getChanges', 'getCatalog', 'listSessions', 'getMaterialEvents', 'prepareReplay'];
  window.atlasHost = { request: async request => {
    fixture.calls.push({ method: request.method, args: structuredClone(request.args) });
    let result;
    if (request.method === 'getCapabilities') result = { protocolVersion: 1, implementation: 'native', supportedMethods: methods, audioAvailable: false, libraryAvailable: true, maxEventBatch: 500, maxVisibleMarkers: 400 };
    else if (request.method === 'getChanges') result = { storeId: 'synthetic-revision', nextSequence: '0', events: [] };
    else if (request.method === 'getCatalog') result = { species: [{ id: 'synthetic-bird', commonName: 'Synthetic bird', scientificName: '', group: 'bird' }], stations: [], sources: [{ id: 'synthetic-revision', name: 'Synthetic source' }], limited: false };
    else if (request.method === 'listSessions') result = [session];
    else if (request.method === 'prepareReplay') { fixture.prepares++; result = { preparationId: 'synthetic-preparation', generation: '1', durationUs: '1000000', eventCount: 2, peakEventsPerSecond: 2 }; }
    else if (request.method === 'getMaterialEvents') {
      if (fixture.fail && request.args.offset > 0) {
        await new Promise(resolve => fixture.deferred.push(resolve));
        return { requestId: request.requestId, ok: false, error: { code: 'stale_generation', message: 'Synthetic material revision changed' } };
      }
      result = { events: [event(request.args.offset)], total: 2, revision: fixture.fail ? '1' : '2' };
    } else throw Error(`Unexpected material fixture method: ${request.method}`);
    return { requestId: request.requestId, ok: true, result };
  } };
});
try {
  await page.goto(origin);
  await page.getByRole('radio', { name: 'Replay', exact: true }).click();
  await page.getByRole('button', { name: 'Revision fixture 2 records', exact: true }).click();
  await page.getByRole('tab', { name: 'Events 1', exact: true }).waitFor();
  await page.waitForFunction(() => window.materialRevisionTest.deferred.length === 1);
  await page.evaluate(() => window.materialRevisionTest.deferred.splice(0).forEach(resolve => resolve()));
  await page.getByRole('alert').filter({ hasText: 'Material could not be loaded' }).waitFor();
  await page.getByRole('tab', { name: 'Events 0', exact: true }).waitFor();
  await page.getByRole('button', { name: 'Map details and coverage', exact: true }).click();
  await page.getByText('0 records · 0 of 0 locations shown.', { exact: true }).waitFor();
  await page.keyboard.press('Escape');
  await page.evaluate(() => { window.materialRevisionTest.fail = false; });
  await page.getByRole('button', { name: 'Retry material', exact: true }).click();
  await page.getByRole('tab', { name: 'Events 2', exact: true }).waitFor();
  await page.getByRole('alert').filter({ hasText: 'Material could not be loaded' }).waitFor({ state: 'hidden' });
  const requests = await page.evaluate(() => window.materialRevisionTest.calls.filter(call => call.method === 'getMaterialEvents').map(call => call.args));
  assert.deepEqual(requests.map(args => [args.offset, args.revision ?? null]), [[0, null], [1, '1'], [0, null], [1, '2']]);
  assert.equal(await page.evaluate(() => window.materialRevisionTest.prepares), 1, 'Retry reloads events without repreparing the selected piece');
  assert.deepEqual(errors, []); assert.deepEqual(external, []); assert.deepEqual(bridge, []);
  console.log(JSON.stringify({ checks: ['first material page displayed before stale second-page response', 'stale_generation clears partial events, counts and map locations', 'failure remains visible', 'explicit retry restarts at offset zero and forwards a new revision', 'selected piece remains prepared once'], pageErrors: errors, external, bridge }));
} finally { await browser.close(); }
