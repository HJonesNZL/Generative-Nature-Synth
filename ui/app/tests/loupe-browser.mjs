// Isolated injected host; synthetic provenance, no database, collection or audio access.
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const origin = process.env.ATLAS_TEST_URL || 'http://127.0.0.1:5173';
const output = resolve(process.env.ATLAS_LOUPE_EVIDENCE || 'test-results/loupe');
const browser = await chromium.launch({ headless: true, channel: 'chrome', args: ['--enable-unsafe-swiftshader'] });
const page = await browser.newPage({ viewport: { width: 1440, height: 900 }, hasTouch: true });
const cdp = await page.context().newCDPSession(page);
const errors = [], external = [], unexpected = [], checks = [];
page.on('pageerror', error => errors.push(error.message));
page.setDefaultTimeout(10000);
page.on('request', request => {
  if (/^https?:/.test(request.url()) && new URL(request.url()).origin !== new URL(origin).origin) external.push(request.url());
  if (request.url().includes('/__atlas/')) unexpected.push(request.url());
});
await page.addInitScript(() => {
  const stamp = '2020-01-01T00:00:00.000000Z';
  const stations = [ ['north', 'North fixture', -38.13, 176.25], ['south', 'South fixture', -46.4, 168.35] ].map(([id, name, latitude, longitude]) => ({ id, name, latitude, longitude, sourceId: 'loupe-fixture', positionDescription: 'Synthetic test position' }));
  const events = stations.map((station, i) => ({ id: `loupe-event-${station.id}`, revisionSequence: String(i + 1), sourceId: 'loupe-fixture', speciesId: 'fixture-bird', commonName: 'Fixture bird', stationId: station.id, eventKind: 'acoustic_species_detection', locationKind: 'station_detection', position: { latitude: station.latitude, longitude: station.longitude, uncertaintyMetres: null }, observedAtUtc: stamp, firstSeenAtUtc: stamp, confidence: null, attribution: 'Synthetic test fixture', qualityFlags: [], provenance: 'demo' }));
  const methods = ['getCapabilities', 'getChanges', 'getCatalog', 'getSourceHealth', 'listRegions'];
  window.atlasHost = { request: async request => {
    let result;
    if (request.method === 'getCapabilities') result = { protocolVersion: 1, implementation: 'native', supportedMethods: methods, audioAvailable: false, libraryAvailable: true, maxEventBatch: 500, maxVisibleMarkers: 400 };
    else if (request.method === 'getChanges') result = { storeId: 'loupe-fixture', nextSequence: '2', events: request.args.afterSequence === '0' ? events.map(event => ({ event, liveEligible: false, change: 'baseline' })) : [] };
    else if (request.method === 'getCatalog') result = { species: [{ id: 'fixture-bird', commonName: 'Fixture bird', scientificName: '', group: 'bird' }], stations, sources: [{ id: 'loupe-fixture', name: 'Synthetic fixture' }], limited: false };
    else if (request.method === 'getSourceHealth') result = [];
    else if (request.method === 'listRegions') result = [{ id: 'fixture-region', name: 'Fixture region', priority: 0, overlapPolicy: 'highest_priority', effectAssignments: [], geometry: { type: 'MultiPolygon', coordinates: [[[[175, -37], [177, -37], [177, -39], [175, -39], [175, -37]]]] } }];
    else throw Error(`Unexpected loupe fixture method: ${request.method}`);
    return { requestId: request.requestId, ok: true, result: structuredClone(result) };
  } };
});
const surface = page.getByRole('group', { name: 'Pan New Zealand detail map', exact: true });
const svg = page.locator('.loupe .map2d');
const reset = page.getByRole('button', { name: 'Reset New Zealand view', exact: true });
const view = () => svg.getAttribute('viewBox');
const picked = () => page.locator('.loupe .map2d__marker[aria-pressed="true"]').evaluateAll(elements => elements[0]?.getAttribute('aria-label') ?? null);
const north = () => page.locator('.loupe .map2d__marker[aria-label^="North fixture receiver,"]');
async function centre(locator) {
  const box = await locator.boundingBox(); assert.ok(box);
  return { x: box.x + box.width / 2, y: box.y + box.height / 2 };
}
async function mouseDrag(start, dx, dy) {
  await page.mouse.move(start.x, start.y); await page.mouse.down();
  await page.mouse.move(start.x + dx, start.y + dy, { steps: 8 }); await page.mouse.up();
}
async function touch(type, x = 0, y = 0) {
  await cdp.send('Input.dispatchTouchEvent', { type, touchPoints: type === 'touchEnd' || type === 'touchCancel' ? [] : [{ x, y, id: 8 }] });
}
try {
  await mkdir(output, { recursive: true });
  await page.goto(origin); await north().waitFor(); await page.locator('.map-stage canvas').waitFor();
  await page.evaluate(() => document.fonts.ready);
  const original = await view();
  const markerCoordinates = await north().locator('circle.map2d__station').evaluate(el => [el.getAttribute('cx'), el.getAttribute('cy')]);
  const coastPath = await svg.locator('.map2d__land').getAttribute('d');
  const regionPath = await svg.locator('.map2d__region').getAttribute('d');
  const frame = await page.locator('.loupe').boundingBox();
  await page.locator('.map-stage canvas').screenshot({ mask: [page.locator('.loupe')], maskColor: '#ffffff' });
  const globe = await page.locator('.map-stage canvas').screenshot({ mask: [page.locator('.loupe')], maskColor: '#ffffff' });
  await mouseDrag(await centre(north()), 35, -30);
  assert.notEqual(await view(), original); assert.equal(await picked(), null, 'drag on a marker must not select it');
  assert.deepEqual(await page.locator('.loupe').boundingBox(), frame, 'circular frame stays fixed');
  assert.equal(await svg.locator('.map2d__land').getAttribute('d'), coastPath);
  assert.equal(await svg.locator('.map2d__region').getAttribute('d'), regionPath);
  assert.deepEqual(await north().locator('circle.map2d__station').evaluate(el => [el.getAttribute('cx'), el.getAttribute('cy')]), markerCoordinates);
  assert.equal((await page.locator('.map-stage canvas').screenshot({ mask: [page.locator('.loupe')], maskColor: '#ffffff' })).equals(globe), true, 'loupe drag leaves globe orientation/render unchanged');
  checks.push('Mouse drag moves one geographic viewport; frame, true marker/region/coast coordinates and globe stay unchanged; drag does not pick');
  await reset.click();
  await mouseDrag(await centre(surface), -400, -350);
  assert.notEqual(await view(), original, 'captured drag continues outside the circle');
  assert.equal(await surface.evaluate(el => el.classList.contains('is-dragging')), false, 'outside release finishes drag');
  assert.equal(await picked(), null);
  assert.equal((await page.locator('.map-stage canvas').screenshot({ mask: [page.locator('.loupe')], maskColor: '#ffffff' })).equals(globe), true, 'captured outside drag does not rotate or pick on globe');
  checks.push('Capture retains loupe ownership outside the circle and releases without affecting globe');
  await reset.click(); assert.equal(await view(), original);
  await mouseDrag(await centre(north()), 2, 1);
  assert.equal(await view(), original, 'sub-threshold movement does not pan');
  assert.match(await picked(), /^North fixture receiver,/, 'ordinary marker click still selects');
  await north().focus(); await page.keyboard.press('Space'); await page.keyboard.press('Enter');
  assert.match(await picked(), /^North fixture receiver,/, 'keyboard marker activation remains available');
  await surface.focus(); await page.keyboard.press('ArrowUp');
  const keyboardPan = await view(); assert.notEqual(keyboardPan, original);
  await page.getByRole('button', { name: 'Sound Studio', exact: true }).click();
  await page.getByRole('button', { name: 'Atlas', exact: true }).click();
  await surface.waitFor(); assert.equal(await view(), keyboardPan, 'pan survives screen navigation');
  await surface.focus(); await page.keyboard.press('Home'); assert.equal(await view(), original);
  checks.push('Click threshold and marker click/keyboard work; arrows pan, Home/reset restore view, screen navigation preserves pan');
  let point = await centre(surface);
  await touch('touchStart', point.x, point.y); await touch('touchMove', point.x + 30, point.y - 65); await touch('touchEnd');
  assert.notEqual(await view(), original, 'touch pans');
  assert.equal(await surface.evaluate(el => el.classList.contains('is-dragging')), false);
  await reset.click();
  await touch('touchStart', point.x, point.y); await touch('touchMove', point.x - 25, point.y - 30); await touch('touchCancel');
  assert.equal(await surface.evaluate(el => el.classList.contains('is-dragging')), false, 'touch cancellation clears drag');
  const cancelled = await view(); await page.mouse.move(point.x + 80, point.y + 80); assert.equal(await view(), cancelled);
  await reset.click();
  await page.mouse.move(point.x, point.y); await page.mouse.down(); await page.mouse.move(point.x + 15, point.y - 15);
  await surface.evaluate(el => { for (let id = 1; id < 20; id++) if (el.hasPointerCapture(id)) el.releasePointerCapture(id); });
  await page.mouse.move(point.x + 40, point.y - 40);
  const lost = await view(); await page.mouse.move(point.x + 70, point.y - 70); await page.mouse.up();
  assert.equal(await view(), lost, 'lost capture stops further moves');
  assert.equal(await surface.evaluate(el => el.classList.contains('is-dragging')), false);
  await reset.click();
  await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', x: point.x, y: point.y, button: 'left', buttons: 1, clickCount: 1, pointerType: 'pen' });
  await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: point.x + 20, y: point.y - 25, button: 'left', buttons: 1, pointerType: 'pen' });
  await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: point.x + 20, y: point.y - 25, button: 'left', buttons: 0, clickCount: 1, pointerType: 'pen' });
  assert.notEqual(await view(), original, 'stylus pans');
  checks.push('Actual touch and pen input pan; touchcancel and lostpointercapture stop ownership and leave future interaction usable');
  await reset.click(); await surface.focus();
  for (let i = 0; i < 10; i++) await page.keyboard.press('Shift+ArrowUp');
  const bounded = await view();
  await page.keyboard.press('Shift+ArrowUp'); assert.equal(await view(), bounded, 'pan clamps at geographic bound');
  const south = page.locator('.loupe .map2d__marker[aria-label^="South fixture receiver,"]'); await south.waitFor(); await south.focus(); await page.keyboard.press('Enter');
  assert.match(await picked(), /^South fixture receiver,/, 'South Island marker remains selectable after pan');
  await page.screenshot({ path: resolve(output, 'south-island-detail.png') });
  await reset.click();
  for (const [width, height] of [[1280, 800], [1440, 900], [1400, 852]]) {
    await page.setViewportSize({ width, height }); await surface.waitFor();
    assert.equal(await surface.evaluate(el => getComputedStyle(el).touchAction), 'none');
    assert.equal(await svg.evaluate(el => getComputedStyle(el).width === getComputedStyle(el).height), true, 'SVG fills square circular viewport');
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
    await page.screenshot({ path: resolve(output, `loupe-${width}x${height}.png`) });
  }
  await page.getByRole('radio', { name: '2D map', exact: true }).click();
  const world = page.getByRole('group', { name: 'World map', exact: true }); await world.waitFor();
  const worldView = await world.getAttribute('viewBox');
  assert.equal(await page.locator('.loupe').count(), 0);
  assert.notEqual(await world.evaluate(el => getComputedStyle(el).touchAction), 'none', 'loupe touch policy does not leak to fallback');
  await world.locator('.map2d__marker').first().focus(); await page.keyboard.press('Enter'); assert.equal(await world.getAttribute('viewBox'), worldView);
  checks.push('Bounded pans reach South Island; target viewports preserve circular scale/layout; world fallback stays independent and keyboard selectable');
  assert.deepEqual(errors, []); assert.deepEqual(external, []); assert.deepEqual(unexpected, []);
  const result = { fixture: 'Synthetic injected native host; no real data or audio access', checks, errors, external, unexpected };
  await writeFile(resolve(output, 'results.json'), JSON.stringify(result, null, 2) + '\n'); console.log(JSON.stringify(result));
} catch (error) { console.error(JSON.stringify({ errors, body: await page.locator('body').innerText(), markers: await page.locator('.map2d__marker').evaluateAll(elements => elements.map(el => el.getAttribute('aria-label'))) })); await page.screenshot({ path: resolve(output, 'failure.png') }); throw error; } finally { await browser.close(); }
