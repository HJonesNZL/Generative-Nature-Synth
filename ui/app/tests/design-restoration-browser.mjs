// Native-like injected fixture: no database, collector, audio device or remote requests.
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const origin = process.env.ATLAS_TEST_URL || 'http://127.0.0.1:5173';
const output = resolve(process.env.ATLAS_DESIGN_EVIDENCE || 'test-results/design-restoration');
const browser = await chromium.launch({ headless: true, channel: 'chrome', args: ['--enable-unsafe-swiftshader'] });
const errors = [], external = [], unexpected = [], checks = [], layouts = [];
await mkdir(output, { recursive: true });
const page = await browser.newPage({ viewport: { width: 1440, height: 900 }, timezoneId: 'Pacific/Auckland' });
page.setDefaultTimeout(10000);
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => { if (/^https?:/.test(request.url()) && new URL(request.url()).origin !== new URL(origin).origin) external.push(request.url()); if (request.url().includes('/__atlas/')) unexpected.push(request.url()); });
await page.addInitScript(() => {
  const state = window.designFixture = { calls: [], failedComposition: false, deviceLost: false, prepared: null };
  const stamp = '2020-01-01T00:00:00.000000Z';
  const species = [ ['tui', 'Tūī'], ['sparrow', 'House sparrow'], ['robin', 'North Island robin'], ['whale', 'Humpback whale'] ].map(([id, commonName]) => ({ id, commonName, scientificName: '', group: id === 'whale' ? 'marine' : 'bird' }));
  const events = species.map((s, i) => ({ id: `design-event-${i}`, revisionSequence: String(i + 1), sourceId: 'design-fixture', speciesId: s.id, commonName: s.commonName, individualId: i === 3 ? 'fixture-tag' : null, deploymentId: i === 3 ? 'fixture-deployment' : null, stationId: i === 3 ? null : 'receiver', eventKind: i === 3 ? 'satellite_fix' : 'acoustic_species_detection', locationKind: i === 3 ? 'individual_fix' : 'station_detection', position: { latitude: [-38, -41, -36, -22][i], longitude: [176, 174, 172, -175][i], uncertaintyMetres: i === 3 ? 1000 : null }, observedAtUtc: stamp, firstSeenAtUtc: stamp, confidence: null, attribution: 'Synthetic layout test fixture', qualityFlags: [], provenance: 'demo' }));
  const session = { id: 'design-piece', name: 'Design fixture', state: 'frozen', origin: 'archive_snapshot', eventCount: events.length, captureTruncated: false, metadataIntegrity: 'sha256-envelope-v1-verified', observedBoundsUs: ['1577836800000000', '1577836801000000'], arrivalBoundsUs: ['1577836800000000', '1577836801000000'] };
  const settings = { enabled: false, tempoBpm: 90, tonicPitchClass: 0, scale: 'majorPentatonic', density: 1, variation: .25, seed: 1, quantize: true };
  const scene = { id: 'a'.repeat(32), name: 'Saved design fixture', valid: true, contentVerified: true, error: null, contentHash: 'c'.repeat(64), sourceSessionId: session.id, eventCount: events.length, durationUs: '1000000', basis: 'observed', speedNumerator: '60', speedDenominator: '1' };
  const methods = ['getCapabilities', 'getChanges', 'getCatalog', 'getSourceHealth', 'listSessions', 'prepareReplay', 'getMaterialEvents', 'getAudioState', 'getTransport', 'getVoiceLibrary', 'getVoice', 'getComposition', 'setComposition', 'listSoundscapes', 'saveSoundscape', 'recallSoundscape', 'startSoundscapeRender'];
  window.atlasHost = { request: async request => {
    const { method, args } = request; state.calls.push({ method, args }); let result;
    if (method === 'getCapabilities') result = { protocolVersion: 1, implementation: 'native', supportedMethods: methods, audioAvailable: true, libraryAvailable: true, maxEventBatch: 500, maxVisibleMarkers: 500 };
    else if (method === 'getChanges') result = { storeId: 'design-fixture', nextSequence: '4', events: args.afterSequence === '0' ? events.map(event => ({ event, liveEligible: false, change: 'baseline' })) : [] };
    else if (method === 'getCatalog') result = { species, stations: [{ id: 'receiver', name: 'Fixture receiver', sourceId: 'design-fixture', latitude: -38, longitude: 176, positionDescription: 'Acoustic receiver' }], sources: [{ id: 'design-fixture', name: 'Synthetic fixture' }], limited: false };
    else if (method === 'getSourceHealth') result = [{ sourceId: 'design-fixture', connection: 'connected', lastSuccessfulFetchUtc: stamp, newestObservationUtc: stamp, publicationLagSeconds: null, coverage: 'best_effort', permissionsStatus: 'Synthetic fixture only', errorCode: null }];
    else if (method === 'listSessions') result = [session];
    else if (method === 'prepareReplay') result = state.prepared = { preparationId: 'design-prepared', generation: '1', durationUs: '1000000', eventCount: events.length, peakEventsPerSecond: 4, session };
    else if (method === 'getMaterialEvents') result = { total: events.length, events: events.slice(args.offset, args.offset + args.limit) };
    else if (method === 'getTransport') result = { preparationId: state.prepared?.preparationId ?? null, generation: state.prepared?.generation ?? '0', state: 'stopped', positionUs: '0', durationUs: state.prepared?.durationUs ?? '0' };
    else if (method === 'getAudioState') result = { status: state.deviceLost ? 'device_lost' : 'disabled', deviceName: null, masterGainDb: -18, peakDbfs: null, meterIsSimulated: false, clipping: false, activeSynthVoices: 0, maxSynthVoices: 8 };
    else if (method === 'listSoundscapes') result = { items: [scene], limited: false };
    else if (method === 'getComposition') result = { settings, preparation: state.prepared, generatedNotes: 4, suppressed: 0, suppressedSourceEvents: 0 };
    else if (method === 'setComposition') { state.failedComposition = true; return { requestId: request.requestId, ok: false, error: { code: 'unavailable', message: 'Fixture composition could not be applied' } }; }
    else if (method === 'getVoiceLibrary') result = { entries: [{ groupId: 'birds', groupName: 'Species voices', groupVoiceId: null, species: species.map(s => ({ speciesId: s.id, commonName: s.commonName, scientificName: s.scientificName, voiceId: s.id, individuals: [] })) }], parameters: [{ id: 'tuning', label: 'Transpose', unit: 'st', min: -12, max: 12, defaultValue: 0, step: 1, curve: 'linear', proposal: false }, { id: 'attack', label: 'Attack', unit: 's', min: .002, max: .1, defaultValue: .008, curve: 'log', proposal: false }, { id: 'decay', label: 'Decay T60', unit: 's', min: .1, max: 3, defaultValue: .65, curve: 'log', proposal: false }, { id: 'gain', label: 'Gain', unit: 'dB', min: -60, max: 0, defaultValue: 0, curve: 'linear', proposal: false }], types: [{ id: 'struck', label: 'Struck', proposal: false }], editingBehavior: 'new_notes' };
    else if (method === 'getVoice') result = { id: args.voiceId, name: species.find(s => s.id === args.voiceId)?.commonName ?? 'Fixture voice', scope: 'species', targetId: args.voiceId, inheritsFromId: null, type: 'struck', savedRevision: '1', changeRevision: '0', dirty: false, values: { tuning: .5, attack: .4, decay: .5, gain: .7 }, overriddenParameterIds: [] };
    else throw Error(`Unexpected design fixture method: ${method}`);
    return { requestId: request.requestId, ok: true, result: structuredClone(result) };
  } };
});
const button = name => page.getByRole('button', { name, exact: true });
const tools = page.locator('details.project-tools');
const summary = page.getByLabel('Project', { exact: true });
async function project(open) { if (await tools.evaluate(element => element.open) !== open) await summary.click(); }
async function layout(screen, width, height) {
  await project(false); await page.setViewportSize({ width, height }); await button(screen).click();
  if (screen === 'Atlas') await page.locator('.map-stage canvas').waitFor();
  else { await page.getByRole('button', { name: /^Tūī species voice/ }).click(); await page.getByRole('heading', { name: 'Tūī', exact: true }).waitFor(); }
  await page.evaluate(() => document.fonts.ready);
  assert.equal(await page.getByRole('region', { name: 'Soundscapes', exact: true }).isVisible(), false);
  assert.equal(await page.getByRole('region', { name: 'Scene composition', exact: true }).isVisible(), false);
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true, `${screen} fits ${width} horizontally`);
  const main = await page.locator('main').boundingBox();
  assert.ok(main.y + main.height <= height + 1, `${screen} fits ${height} vertically`);
  const canvas = screen === 'Atlas' ? await page.locator('.map-stage canvas').boundingBox() : null;
  if (canvas) assert.ok(canvas.height >= (width === 1440 ? 450 : 360), `Globe canvas remains dominant: ${canvas.height}`);
  const screenshot = resolve(output, `${screen === 'Atlas' ? 'atlas' : 'studio'}-${width}x${height}.png`);
  await page.screenshot({ path: screenshot }); layouts.push({ screen, width, height, main, canvas, screenshot });
}
try {
  await page.goto(origin); await page.getByRole('button', { name: 'All available species', exact: true }).waitFor();
  await page.getByRole('radio', { name: 'Replay', exact: true }).click(); await page.getByRole('button', { name: /^Design fixture/ }).click();
  await page.waitForFunction(() => designFixture.prepared !== null);
  for (const [width, height] of [[1440, 900], [1280, 800], [1400, 852]]) for (const screen of ['Atlas', 'Sound Studio']) await layout(screen, width, height);
  checks.push('Atlas and Studio fit all target viewports with collapsed global tools', 'Globe canvas height >=450 at 1440×900 and >=360 at 1280×800');
  await summary.focus(); await page.keyboard.press('Enter');
  assert.equal(await tools.evaluate(element => element.open), true, 'Enter opens Project');
  const tempo = page.getByRole('spinbutton', { name: 'Composition tempo BPM', exact: true });
  await tempo.fill('117'); await tempo.focus(); await page.keyboard.press('Escape');
  assert.equal(await tools.evaluate(element => element.open), false);
  assert.equal(await summary.evaluate(element => element === document.activeElement), true, 'Escape returns focus to Project');
  await project(true); assert.equal(await tempo.inputValue(), '117', 'composition draft survives closing Project');
  assert.equal(await page.evaluate(() => designFixture.calls.some(call => call.method === 'setComposition')), false, 'closing Project does not apply a draft');
  await page.keyboard.press('Escape'); await button('Atlas').click(); await project(true); assert.equal(await tempo.inputValue(), '117', 'composition draft survives screen navigation');
  checks.push('Project is keyboard accessible, Escape closes and restores focus, composition draft persists without mutation');
  await button('Apply composition').click(); await page.getByText('Fixture composition could not be applied', { exact: true }).waitFor();
  await page.screenshot({ path: resolve(output, 'project-composition-error.png') });
  await page.keyboard.press('Escape'); await page.evaluate(() => { designFixture.deviceLost = true; });
  await page.getByText('Audio device lost', { exact: true }).waitFor();
  assert.equal(await tools.evaluate(element => element.open), false);
  assert.equal(await page.getByText('Audio device lost', { exact: true }).isVisible(), true, 'actual playback failures stay visible outside Project');
  await page.screenshot({ path: resolve(output, 'visible-device-error.png') });
  checks.push('Composition failures are visible in Project; actual audio loss remains visible while Project is closed');
  assert.deepEqual(errors, []); assert.deepEqual(external, []); assert.deepEqual(unexpected, []);
  const result = { origin, browser: browser.version(), fixture: 'Injected native-like host; synthetic event provenance; no real audio', checks, layouts, errors, external, unexpected, references: ['design/claude-handoff/atlas-live-1440x900.png', 'design/claude-handoff/studio-1440x900.png', 'Animal_Atlas_Direction_H_Design_Handoff/images/01_H_Atlas_History.png', 'Animal_Atlas_Direction_H_Design_Handoff/images/02_H_Sound_Studio.png'] };
  await writeFile(resolve(output, 'results.json'), JSON.stringify(result, null, 2) + '\n'); console.log(JSON.stringify(result));
} finally { await browser.close(); }
