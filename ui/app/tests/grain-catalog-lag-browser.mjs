// Isolated synthetic reports with an initially empty catalogue. No providers/audio.
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
  const fixture = window.grainCatalogTest = { catalogMode: 'empty', calls: [], baseline: false };
  const now = Date.now();
  const event = (species, index, commonName = species === 3 ? null : `Fixture species ${species}`) => ({ id: `grain-event-${index}`, revisionSequence: String(index + 1), sourceId: 'synthetic-grain', speciesId: species === null ? null : `grain-species-${species}`, commonName, stationId: 'grain-receiver', individualId: null, deploymentId: null, eventKind: 'acoustic_species_detection', locationKind: 'station_detection', position: { latitude: -40, longitude: 175, uncertaintyMetres: null }, observedAtUtc: new Date(now - 90000 + index * 1000).toISOString(), firstSeenAtUtc: new Date(now - 60000 + index * 1000).toISOString(), confidence: null, attribution: 'Synthetic Grain fixture', qualityFlags: [], provenance: 'demo' });
  const events = Array.from({ length: 8 }, (_, species) => event(species, species));
  events[0].commonName = null;
  events.push(event(0, 8), event(null, 9, null));
  const methods = ['getCapabilities', 'getChanges', 'getCatalog'];
  window.atlasHost = { request: async request => {
    fixture.calls.push({ method: request.method, args: structuredClone(request.args) });
    let result;
    if (request.method === 'getCapabilities') result = { protocolVersion: 1, implementation: 'native', supportedMethods: methods, audioAvailable: false, libraryAvailable: true, maxEventBatch: 500, maxVisibleMarkers: 400 };
    else if (request.method === 'getChanges') {
      const baseline = !fixture.baseline;
      fixture.baseline = true;
      result = { storeId: 'grain-fixture', nextSequence: '10', events: baseline ? events.map(event => ({ event, liveEligible: false, change: 'baseline' })) : [] };
    } else if (request.method === 'getCatalog') result = { species: fixture.catalogMode === 'empty' ? [] : [{ id: 'grain-species-2', commonName: 'Catalogue species 2', scientificName: '', group: 'bird' }, { id: 'grain-species-0', commonName: 'Catalogue species 0', scientificName: '', group: 'bird' }], stations: [], sources: [], limited: false };
    else throw Error(`Unexpected Grain fixture method: ${request.method}`);
    return { requestId: request.requestId, ok: true, result };
  } };
});
const grain = page.getByRole('region', { name: 'Detections by species', exact: true });
const names = () => grain.locator('.grain__name').evaluateAll(elements => elements.map(element => element.childNodes[0].textContent));
const paths = () => grain.locator('.grain__ticks > path:first-child').evaluateAll(elements => elements.map(element => element.getAttribute('d')));
try {
  await page.goto(origin);
  await page.waitForFunction(() => document.querySelectorAll('.grain__lane').length === 6);
  assert.deepEqual(await names(), ['Fixture species 0', 'Fixture species 1', 'Fixture species 2', 'grain-species-3', 'Fixture species 4', 'Fixture species 5']);
  assert.equal(await grain.getByText('No records in this window · coverage may be incomplete.', { exact: true }).count(), 0);
  const reportedPaths = await paths();
  assert.ok(reportedPaths.every(path => /^M[0-9.]+ 0v26/.test(path)), 'Each displayed species has real reported-event lines');
  assert.equal((reportedPaths[0].match(/M/g) || []).length, 2, 'Repeated reports share their genuine species lane');
  assert.ok(reportedPaths.slice(1).every(path => (path.match(/M/g) || []).length === 1));
  assert.equal(await grain.getByRole('button', { name: /Fixture species 6|Fixture species 7/ }).count(), 0, 'Six-lane display cap is preserved');
  assert.equal(await grain.getByRole('button', { name: /Unknown species|Fixture species null/ }).count(), 0, 'Missing species identity does not fabricate a lane');
  assert.ok(await page.evaluate(() => window.grainCatalogTest.calls.some(call => call.method === 'getCatalog')));
  assert.equal(await page.evaluate(() => window.grainCatalogTest.catalogMode), 'empty', 'Grain renders before any catalogue refresh');
  await grain.locator('.grain__name').first().click();
  assert.equal(await grain.locator('.grain__name').first().getAttribute('aria-pressed'), 'true');
  await page.evaluate(() => { window.grainCatalogTest.catalogMode = 'known'; });
  await page.getByRole('button', { name: 'Sound Studio', exact: true }).click();
  await grain.getByRole('button', { name: /^Catalogue species 2/ }).waitFor();
  assert.deepEqual(await names(), ['Catalogue species 2', 'Catalogue species 0', 'Fixture species 1', 'grain-species-3', 'Fixture species 4', 'Fixture species 5']);
  assert.equal(await grain.locator('.grain__lane').count(), 6);
  assert.ok((await paths()).every(path => path.length > 0));
  await page.getByRole('button', { name: 'Atlas', exact: true }).click();
  await grain.getByRole('button', { name: /^Catalogue species 2/ }).waitFor();
  assert.deepEqual(await names(), ['Catalogue species 2', 'Catalogue species 0', 'Fixture species 1', 'grain-species-3', 'Fixture species 4', 'Fixture species 5']);
  assert.deepEqual(errors, []); assert.deepEqual(external, []); assert.deepEqual(bridge, []);
  console.log(JSON.stringify({ checks: ['nonempty reports immediately draw six Grain lanes with an empty startup catalogue', 'event common names used and missing names retain genuine species ID', 'repeated reports share a lane with one line per report', 'missing identity does not invent a lane', 'catalogue order and names take priority when available', 'lane highlighting and Atlas/Studio continuity remain available'], pageErrors: errors, external, bridge }));
} finally { await browser.close(); }
