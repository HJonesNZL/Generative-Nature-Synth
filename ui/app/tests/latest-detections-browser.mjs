// Isolated synthetic host: no owner database, collection, provider or audio access.
import assert from 'node:assert/strict';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const origin = process.env.ATLAS_TEST_URL || 'http://127.0.0.1:5173';
const browser = await chromium.launch({ headless: true, channel: 'chrome', args: ['--enable-unsafe-swiftshader'] });
const page = await browser.newPage({ viewport: { width: 1440, height: 900 }, reducedMotion: 'reduce' });
const errors = [], external = [], bridge = [];
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => {
  if (/^https?:/.test(request.url()) && new URL(request.url()).origin !== new URL(origin).origin) external.push(request.url());
  if (request.url().includes('/__atlas/')) bridge.push(request.url());
});
page.setDefaultTimeout(10000);
await page.addInitScript(() => {
  const stamp = new Date(Date.now() - 30000).toISOString();
  const stations = [ ['north', 'North fixture', -38.13, 176.25], ['south', 'South fixture', -46.4, 168.35] ].map(([id, name, latitude, longitude]) => ({ id, name, latitude, longitude, sourceId: 'loupe-fixture', positionDescription: 'Synthetic test position' }));
  const events = stations.map((station, i) => ({ id: `loupe-event-${station.id}`, revisionSequence: String(i + 1), sourceId: 'loupe-fixture', speciesId: 'fixture-bird', commonName: 'Fixture bird', stationId: station.id, eventKind: 'acoustic_species_detection', locationKind: 'station_detection', position: { latitude: station.latitude, longitude: station.longitude, uncertaintyMetres: null }, observedAtUtc: new Date(Date.parse(stamp) + i * 1000).toISOString(), firstSeenAtUtc: new Date(Date.parse(stamp) + i * 1000).toISOString(), confidence: null, attribution: 'Synthetic test fixture', qualityFlags: [], provenance: 'demo' }));
  events.push({ ...events[0], id: 'overseas-fix', revisionSequence: '3', speciesId: 'fixture-whale', commonName: 'Fixture whale', stationId: null, individualId: 'Fixture whale individual', eventKind: 'gps_fix', locationKind: 'animal_location', position: { latitude: 40, longitude: -70, uncertaintyMetres: 30 }, observedAtUtc: stamp, firstSeenAtUtc: new Date().toISOString() });
  const fixture = window.latestDetectionsFixture = { pending: [], sequence: '3', add: () => { fixture.pending.push({ ...events[0], id: 'new-north', revisionSequence: '4', observedAtUtc: new Date(Date.now() + 1000).toISOString(), firstSeenAtUtc: new Date().toISOString() }); fixture.sequence = '4'; } };
  const methods = ['getCapabilities', 'getChanges', 'getCatalog', 'getSourceHealth', 'listRegions'];
  window.atlasHost = { request: async request => {
    let result;
    if (request.method === 'getCapabilities') result = { protocolVersion: 1, implementation: 'native', supportedMethods: methods, audioAvailable: false, libraryAvailable: true, maxEventBatch: 500, maxVisibleMarkers: 400 };
    else if (request.method === 'getChanges') result = { storeId: 'loupe-fixture', nextSequence: fixture.sequence, events: request.args.afterSequence === '0' ? events.map(event => ({ event, liveEligible: false, change: 'baseline' })) : fixture.pending.splice(0).map(event => ({ event, liveEligible: false, change: 'insert' })) };
    else if (request.method === 'getCatalog') result = { species: [{ id: 'fixture-bird', commonName: 'Fixture bird', scientificName: '', group: 'bird' }, { id: 'fixture-whale', commonName: 'Fixture whale', scientificName: '', group: 'mammal' }], stations, sources: [{ id: 'loupe-fixture', name: 'Synthetic fixture' }], limited: false };
    else if (request.method === 'getSourceHealth') result = [];
    else if (request.method === 'listRegions') result = [{ id: 'fixture-region', name: 'Fixture region', priority: 0, overlapPolicy: 'highest_priority', effectAssignments: [], geometry: { type: 'MultiPolygon', coordinates: [[[[175, -37], [177, -37], [177, -39], [175, -39], [175, -37]]]] } }];
    else throw Error(`Unexpected loupe fixture method: ${request.method}`);
    return { requestId: request.requestId, ok: true, result: structuredClone(result) };
  } };
});

try {
  await page.goto(origin);
  const globe = page.locator('.globe');
  await globe.waitFor();
  await page.waitForFunction(() => document.querySelector('.globe')?.getAttribute('aria-label')?.includes('Latest detection'));
  assert.match(await globe.getAttribute('aria-label'), /Latest detection: South fixture receiver/);
  const loupe = page.locator('.loupe .map2d');
  const originalView = await loupe.getAttribute('viewBox');
  assert.equal(await loupe.locator('.is-recent').count(), 1, 'Recent North Island receiver has a persistent subordinate outline');
  await page.getByRole('radio', { name: '2D map', exact: true }).click();
  const world = page.getByRole('group', { name: 'World map', exact: true });
  await world.waitFor();
  const latest = world.locator('.map2d__marker.is-latest');
  assert.match(await latest.getAttribute('aria-label'), /South fixture receiver.*Latest detection/);
  assert.equal(await world.locator('.is-recent').count(), 2);
  assert.ok(await latest.locator('circle[stroke="#bd6538"]').count());
  await latest.focus(); await page.keyboard.press('Enter');
  assert.equal(await latest.getAttribute('aria-pressed'), 'true', 'Selection remains independent of latest status');
  assert.equal(await latest.locator('.map2d__selection').count(), 1);
  await page.locator('.grain__name').first().click();
  assert.equal(await latest.locator('.map2d__station.is-highlight').count(), 1, 'Species emphasis remains available beneath the persistent latest outline');
  assert.equal(await latest.locator('.map2d__selection').count(), 1, 'Species emphasis preserves the selected event');
  await page.getByRole('radio', { name: 'Globe', exact: true }).click();
  await globe.waitFor();
  assert.equal(await loupe.getAttribute('viewBox'), originalView, 'Selecting the latest marker does not automatically pan the detail');
  // Parent integration provides the explicit locate control; it must reveal the latest South Island receiver.
  await page.getByRole('tab', { name: 'Latest detections', exact: true }).click();
  const reports = page.getByRole('list', { name: 'Latest detections by observation time' });
  const reportButtons = reports.getByRole('button');
  assert.deepEqual(await reportButtons.allTextContents().then(values => values.map(value => /Fixture whale/.test(value))), [false, true, false], 'Observed time wins over reversed arrival order; equal observation times use revision order');
  await reports.getByRole('button', { name: 'Show Fixture bird on map', exact: true }).first().click();
  await page.waitForFunction(() => document.querySelector('.loupe .map2d__marker.is-latest'));
  assert.notEqual(await loupe.getAttribute('viewBox'), originalView, 'Explicit locate reveals latest in the NZ detail');
  assert.match(await loupe.locator('.is-latest').getAttribute('aria-label'), /South fixture receiver/);
  await page.getByRole('tab', { name: 'Latest detections', exact: true }).click();
  const nzGlobe = await page.locator('.globe__canvas').screenshot();
  await reports.getByRole('button', { name: 'Show Fixture whale on map', exact: true }).click();
  const overseasGlobe = await page.locator('.globe__canvas').screenshot();
  assert.equal(overseasGlobe.equals(nzGlobe), false, 'Explicit overseas locate changes globe orientation');
  assert.equal(await loupe.getAttribute('viewBox') === originalView, false, 'Overseas locate preserves NZ detail');
  const locatedView = await loupe.getAttribute('viewBox');
  await page.getByRole('group', { name: 'Pan New Zealand detail map', exact: true }).focus();
  await page.keyboard.press('ArrowDown');
  assert.notEqual(await loupe.getAttribute('viewBox'), locatedView, 'Owner can pan after locating');
  await page.getByRole('button', { name: 'Sound Studio', exact: true }).click();
  await page.getByRole('button', { name: 'Atlas', exact: true }).click();
  await globe.waitFor();
  assert.match(await globe.getAttribute('aria-label'), /Latest detection: South fixture receiver/);
  const manualView = await loupe.getAttribute('viewBox');
  const manualLabel = await page.locator('.globe__label').getAttribute('style');
  await page.evaluate(() => window.latestDetectionsFixture.add());
  await page.waitForFunction(() => document.querySelector('.globe')?.getAttribute('aria-label')?.includes('Latest detection: North fixture receiver'));
  assert.equal(await loupe.getAttribute('viewBox'), manualView, 'New arrivals preserve manual NZ pan');
  assert.equal(await page.locator('.globe__label').getAttribute('style'), manualLabel, 'New arrivals preserve globe orientation and selected-label position');
  assert.deepEqual(errors, []); assert.deepEqual(external, []); assert.deepEqual(bridge, []);
  console.log(JSON.stringify({ checks: ['latest and recent persistent outlines on flat map and loupe', 'latest accessible globe label', 'latest selection and species emphasis have distinct independent rings', 'explicit locate reveals latest NZ position', 'pan remains usable after locate', 'latest survives screen switch', 'reduced motion enabled'], errors, external, bridge }));
} finally { await browser.close(); }
