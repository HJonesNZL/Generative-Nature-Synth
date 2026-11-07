// Deterministic synthetic library only: no database, source requests or audio.
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const origin = process.env.ATLAS_TEST_URL || 'http://127.0.0.1:5173';
const evidence = resolve(process.env.ATLAS_SESSION_EVIDENCE || 'test-results/session-browser');
const browser = await chromium.launch({ headless: true, channel: 'chrome', args: ['--enable-unsafe-swiftshader'] });
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
const errors = [], external = [], bridgeRequests = [];
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => {
  if (/^https?:/.test(request.url()) && new URL(request.url()).origin !== new URL(origin).origin) external.push(request.url());
  if (request.url().includes('/__atlas/')) bridgeRequests.push(request.url());
});
page.setDefaultTimeout(10000);
await page.addInitScript(() => {
  const fixture = window.sessionBrowserTest = { calls: [], revision: '1', sourceId: '0123456789abcdef0123456789abcdef', total: 2607, defer: false, deferred: [], fail: null, prepares: 0, generation: 0 };
  const methods = ['getCapabilities', 'getChanges', 'getCatalog', 'getSourceHealth', 'listSessionsPage', 'getMaterialEvents', 'prepareReplay'];
  const piece = index => ({ id: `piece-${index}`, name: `Synthetic piece ${index}`, state: index === 100 ? 'interrupted' : 'frozen', origin: index === 100 ? 'imported_bundle' : 'archive_snapshot', captureTruncated: index === 100, eventCount: 0 });
  window.atlasHost = { request: async request => {
    fixture.calls.push({ method: request.method, args: structuredClone(request.args) });
    let result;
    if (request.method === 'getCapabilities') result = { protocolVersion: 1, implementation: 'native', supportedMethods: methods, audioAvailable: false, libraryAvailable: true, maxEventBatch: 500, maxVisibleMarkers: 400 };
    else if (request.method === 'getChanges') result = { storeId: fixture.sourceId, nextSequence: '0', events: [] };
    else if (request.method === 'getCatalog') result = { species: [], stations: [], sources: [], limited: false };
    else if (request.method === 'getSourceHealth') result = [];
    else if (request.method === 'getMaterialEvents') result = { events: [], total: 0 };
    else if (request.method === 'prepareReplay') { fixture.prepares++; result = { preparationId: `prep-${++fixture.generation}`, generation: String(fixture.generation), durationUs: '1000000', eventCount: 0, peakEventsPerSecond: 0 }; }
    else if (request.method === 'listSessionsPage') {
      if (fixture.fail) { const error = fixture.fail; fixture.fail = null; return { requestId: request.requestId, ok: false, error }; }
      const cursor = request.args.cursor;
      if (cursor && !cursor.startsWith(`${fixture.sourceId}:${fixture.revision}:`)) return { requestId: request.requestId, ok: false, error: { code: 'stale_generation', message: 'Saved pieces changed; refresh this list.' } };
      const offset = cursor ? Number(cursor.split(':').at(-1)) : 0;
      result = { items: Array.from({ length: Math.min(request.args.limit, fixture.total - offset) }, (_, index) => piece(offset + index)), nextCursor: offset + request.args.limit < fixture.total ? `${fixture.sourceId}:${fixture.revision}:${offset + request.args.limit}` : null, revision: fixture.revision, sourceId: fixture.sourceId };
      if (fixture.defer) { fixture.defer = false; await new Promise(resolve => fixture.deferred.push(resolve)); }
    } else throw Error(`Unexpected synthetic request: ${request.method}`);
    return { requestId: request.requestId, ok: true, result };
  } };
});
const button = name => page.getByRole('button', { name, exact: true });
const ready = number => page.locator('.session-browser [role="status"]').filter({ hasText: `Page ${number} ·` }).waitFor();
const selected = () => page.locator('.session-row[aria-pressed="true"]');
const settle = () => page.evaluate(async () => { await document.fonts.ready; for (let frame = 0; frame < 4; ++frame) await new Promise(resolve => requestAnimationFrame(resolve)); });
try {
  await mkdir(evidence, { recursive: true });
  await page.goto(origin);
  await page.getByRole('radio', { name: 'Replay', exact: true }).click();
  await ready(1);
  assert.equal(await page.locator('.session-row').count(), 50);
  assert.equal(await button('Previous').isDisabled(), true);
  await button('Next').click(); await ready(2);
  await button('Next').click(); await ready(3);
  await page.getByRole('button', { name: /^Synthetic piece 100 Imported Interrupted Truncated 0 records$/ }).click();
  await page.waitForFunction(() => window.sessionBrowserTest.prepares === 1);
  assert.match(await selected().innerText(), /Synthetic piece 100/);
  await selected().click();
  assert.equal(await page.evaluate(() => window.sessionBrowserTest.prepares), 1, 'Selected row does not prepare twice');
  await button('Sound Studio').click(); await button('Atlas').click(); await ready(3);
  assert.match(await selected().innerText(), /Synthetic piece 100/);
  assert.equal(await page.evaluate(() => window.sessionBrowserTest.prepares), 1, 'Atlas/Studio keeps selected material and preparation');
  await button('Previous').click(); await ready(2);
  await page.getByText('Selected: Synthetic piece 100 · outside this page.', { exact: true }).waitFor();
  await button('Next').click(); await ready(3);
  for (let number = 4; number <= 53; ++number) { await button('Next').click(); await ready(number); }
  assert.equal(await page.locator('.session-row').count(), 7);
  assert.equal(await button('Next').isDisabled(), true);
  await page.getByText('Page 53 · 7 pieces · End of list. Total unknown.', { exact: true }).waitFor();
  await button('Refresh pieces').click(); await ready(1);
  await page.evaluate(() => { window.sessionBrowserTest.defer = true; });
  await button('Next').click();
  await page.waitForFunction(() => window.sessionBrowserTest.deferred.length === 1);
  await button('Refresh pieces').click(); await ready(1);
  await page.evaluate(() => window.sessionBrowserTest.deferred.splice(0).forEach(resolve => resolve()));
  await ready(1);
  assert.match(await page.locator('.session-row').first().innerText(), /^Synthetic piece 0\n/);
  await page.evaluate(() => { window.sessionBrowserTest.revision = '2'; });
  await button('Next').click();
  await page.getByRole('alert').filter({ hasText: 'Saved pieces changed; refresh this list.' }).waitFor();
  assert.equal(await button('Next').isDisabled(), true);
  assert.equal(await page.evaluate(() => window.sessionBrowserTest.prepares), 1);
  await button('Refresh pieces').click(); await ready(1);
  await page.evaluate(() => { window.sessionBrowserTest.fail = { code: 'unavailable', message: 'Synthetic library is unreadable.' }; });
  await button('Next').click();
  await page.getByRole('alert').filter({ hasText: 'Synthetic library is unreadable.' }).waitFor();
  await button('Refresh pieces').click(); await ready(1);
  for (const size of [{ width: 1440, height: 900 }, { width: 1280, height: 800 }]) {
    await page.setViewportSize(size);
    await settle();
    await page.locator('.map-stage canvas').screenshot();
    assert.equal(await page.locator('.app-header').count(), 1);
    const left = await page.locator('.atlas__left').boundingBox(), globe = await page.locator('.map-stage').boundingBox();
    assert.ok(globe.width > left.width * 1.4, 'Central globe remains larger than the supporting column');
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
    await page.screenshot({ path: resolve(evidence, `atlas-${size.width}x${size.height}.png`) });
    await button('Sound Studio').click();
    await settle();
    await page.screenshot({ path: resolve(evidence, `studio-${size.width}x${size.height}.png`) });
    await button('Atlas').click(); await ready(1); await settle();
  }
  assert.deepEqual(errors, []); assert.deepEqual(external, []); assert.deepEqual(bridgeRequests, []);
  const calls = await page.evaluate(() => window.sessionBrowserTest.calls);
  assert.ok(calls.filter(call => call.method === 'listSessionsPage').every(call => call.args.limit === 50));
  const result = { checks: ['2607-piece synthetic library reaches final page with bounded 50-row display', 'beyond-first-100 imported/interrupted/truncated selection and prepare once', 'selection and page survive Atlas/Studio', 'previous page and selected-outside-page guidance', 'truthful unknown total and end state', 'late reply cannot overwrite refreshed page', 'stale revision and unreadable library visible recovery', '1440x900 and 1280x800 Atlas/Studio layout'], pageErrors: errors, external, bridgeRequests };
  await writeFile(resolve(evidence, 'result.json'), JSON.stringify(result, null, 2));
  console.log(JSON.stringify(result));
} finally { await browser.close(); }
