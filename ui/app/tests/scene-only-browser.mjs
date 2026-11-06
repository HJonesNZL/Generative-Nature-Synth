// Intentional scene-only startup: no wildlife library, with usable soundscapes/Studio.
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: process.env.ATLAS_BROWSER || 'chrome', args: ['--enable-unsafe-swiftshader'] });
const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
const errors = [], requests = [];
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => requests.push(request.url()));
await page.addInitScript(() => {
  const s = window.sceneOnlyTest = { calls: [], recalled: false };
  const id = '0123456789abcdef0123456789abcdef';
  const scene = { id, name: 'Imported scene fixture', valid: true, contentVerified: true, error: null, contentHash: 'a'.repeat(64), sourceSessionId: 'fixture-source', eventCount: 0, durationUs: '1000000', basis: 'observed', speedNumerator: '1', speedDenominator: '1' };
  const session = { id: `scene:${id}`, name: scene.name, state: 'frozen', origin: 'imported_bundle', eventCount: 0, captureTruncated: false };
  const preparation = { preparationId: 'scene-preparation', generation: '1', durationUs: '1000000', eventCount: 0, peakEventsPerSecond: 0, session };
  window.atlasHost = { request: async request => {
    const { method, args } = request; s.calls.push({ method, args });
    let result;
    if (method === 'getCapabilities') result = { protocolVersion: 1, implementation: 'native', supportedMethods: ['getCapabilities', 'getChanges', 'getCatalog', 'getSourceHealth', 'listSessions', 'getMaterialEvents', 'listSoundscapes', 'recallSoundscape', 'getVoiceLibrary', 'getVoice'], audioAvailable: false, libraryAvailable: false, maxEventBatch: 500, maxVisibleMarkers: 500 };
    else if (method === 'listSoundscapes') result = { items: [scene], limited: false };
    else if (method === 'recallSoundscape') { s.recalled = true; result = { soundscape: scene, preparation }; }
    else if (method === 'getMaterialEvents') result = { total: 0, events: [] };
    else if (method === 'getCatalog' && args.material?.sessionId === session.id) result = { species: [], stations: [], sources: [] };
    else if (method === 'getVoiceLibrary') result = { entries: [{ groupId: 'native', groupName: 'Species voices', groupVoiceId: 'default', species: [] }], parameters: [], editingBehavior: 'new_notes' };
    else if (method === 'getVoice') result = { id: 'default', name: 'Fallback voice', scope: 'group', targetId: 'all_animals', inheritsFromId: null, type: 'struck', savedRevision: '1', changeRevision: '1', dirty: false, values: {}, overriddenParameterIds: [] };
    else return { requestId: request.requestId, ok: false, error: { code: 'unavailable', message: 'Library data could not be verified or read' } };
    return { requestId: request.requestId, ok: true, result };
  } };
});
const button = name => page.getByRole('button', { name, exact: true });
const url = process.env.ATLAS_TEST_URL || 'http://127.0.0.1:4191/';
try {
  await page.goto(url);
  await page.getByText('Scene-only mode', { exact: true }).waitFor();
  await page.getByText('Import a soundscape or choose a wildlife library in the app toolbar.', { exact: true }).waitFor();
  await page.waitForTimeout(2200);
  await page.evaluate(() => document.fonts.ready);
  await mkdir('test-results', { recursive: true });
  await page.screenshot({ path: 'test-results/scene-only-atlas.png' });
  assert.equal(await page.getByText('Arrivals paused', { exact: true }).count(), 0);
  assert.equal(await page.getByText(/Library data could not be verified or read|feed resumes|Nothing is lost/).count(), 0);
  await page.getByRole('radio', { name: 'Replay', exact: true }).click();
  await page.getByText('Choose a wildlife library to list its pieces. Saved soundscapes remain available above.', { exact: true }).waitFor();
  assert.equal(await page.getByText('Could not read saved pieces', { exact: true }).count(), 0);
  await page.getByRole('combobox', { name: 'Saved soundscape', exact: true }).selectOption('0123456789abcdef0123456789abcdef');
  await button('Open soundscape').click();
  await page.waitForFunction(() => window.sceneOnlyTest.recalled);
  await page.getByRole('status').filter({ hasText: 'Opened “Imported scene fixture”' }).waitFor();
  await page.waitForFunction(() => window.sceneOnlyTest.calls.some(call => call.method === 'getCatalog' && call.args.material?.sessionId.startsWith('scene:')));
  await button('Sound Studio').click();
  await button('Fallback voice').click();
  await page.getByRole('heading', { name: 'Fallback voice', exact: true }).waitFor();
  assert.equal(await page.getByRole('region', { name: 'Soundscapes', exact: true }).count(), 1);
  assert.deepEqual(await page.evaluate(() => window.sceneOnlyTest.calls.filter(call => ['getChanges', 'listSessions', 'getSourceHealth'].includes(call.method) || call.method === 'getCatalog' && !call.args.material)), []);
  assert.deepEqual(errors, []);
  assert.ok(requests.every(request => request.startsWith(new URL(url).origin) || request.startsWith('data:')));
  await page.evaluate(() => document.fonts.ready);
  await mkdir('test-results', { recursive: true });
  await page.screenshot({ path: 'test-results/scene-only-studio.png' });
  const result = { origin: url, browser: browser.version(), errors, checks: ['intentional scene-only guidance', 'no live feed poll across its interval', 'no unavailable library-wide catalog/session/source requests', 'no misleading retry or data-loss promises', 'saved soundscape recall remains available', 'scene-specific catalog and material stay available', 'Sound Studio and fallback voice remain available', 'no external requests'], screenshots: ['test-results/scene-only-atlas.png', 'test-results/scene-only-studio.png'] };
  await writeFile('test-results/scene-only-browser.json', JSON.stringify(result, null, 2) + '\n');
  console.log(JSON.stringify(result));
} finally { await browser.close(); }
