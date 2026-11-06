import test from 'node:test';
import assert from 'node:assert/strict';
import { recipeStillCurrent } from '../src/app/recipeIdentity.ts';
import { EditBarrier } from '../src/app/editBarrier.ts';
import { HostClient } from '../src/host/client.ts';
import { spatialDraft, spatialFromDraft, spatialSettingsError, newSpatialRegion } from '../src/host/spatialSettings.ts';
globalThis.window = { setTimeout, clearTimeout };
const settings = { enabled: true, amount: .45, regions: [newSpatialRegion('west', 'Geographic west'), { ...newSpatialRegion('date', 'Dateline 🌊'), west: 170, east: -170 }] };
const prep = { preparationId: 'audio-2', generation: '1', durationUs: '100', eventCount: 2, peakEventsPerSecond: 1 };
const caps = { protocolVersion: 1, implementation: 'native', supportedMethods: ['getCapabilities', 'getSpatial', 'setSpatial'], audioAvailable: false, maxEventBatch: 500, maxVisibleMarkers: 500 };
test('spatial DTO enforces ordered bounded regions, IDs, Unicode and complete finite ranges', () => {
  assert.equal(spatialSettingsError(settings), null);
  const invalid = [
    { ...settings, enabled: 1 }, { ...settings, amount: NaN }, { ...settings, regions: Array(9).fill(settings.regions[0]) },
    { ...settings, regions: [settings.regions[0], settings.regions[0]] },
    ...[{ id: '../x' }, { name: '\ud800' }, { name: 'line\nbreak' }, { name: 'é'.repeat(65) }, { south: 1, north: 0 }, { west: 180, east: -180 }, { west: 0, east: 0 }, { pan: 2 }, { gainDb: .1 }, { send: Infinity }, { echoMix: -1 }, { delayMs: 19.9 }, { feedback: .51 }, { roomSeconds: 2.6 }, { tone: -1 }, { positionPan: 0 }, { unknown: true }].map(patch => ({ ...settings, regions: [{ ...settings.regions[0], ...patch }] })),
  ];
  for (const value of invalid) assert.notEqual(spatialSettingsError(value), null);
});
test('temporary spatial number text survives editing and only complete valid settings convert', () => {
  const draft = spatialDraft(settings); assert.deepEqual(spatialFromDraft(draft), settings);
  draft.regions[0].south = '-'; assert.throws(() => spatialFromDraft(draft), /complete number/); assert.equal(draft.regions[0].south, '-');
  draft.regions[0].south = '-42.75'; assert.equal(spatialFromDraft(draft).regions[0].south, -42.75);
  draft.regions[0].feedback = '.6'; assert.throws(() => spatialFromDraft(draft), /feedback/);
  draft.regions[0].feedback = '.3'; draft.amount = ''; assert.throws(() => spatialFromDraft(draft), /complete number/);
});
test('explicit Apply skips sibling finalizers while Save runs composition then spatial using fresh identity', async () => {
  const barrier = new EditBarrier(), order = []; let identity = 'old';
  barrier.register(async () => { order.push('voices'); return true; });
  barrier.registerFinalizer(async () => { assert.equal(identity, 'composition'); order.push('spatial'); identity = 'spatial'; return true; }, 1);
  barrier.registerFinalizer(async () => { order.push('composition'); identity = 'composition'; return true; }, 0);
  barrier.begin(); assert.equal(await barrier.drain(true, false), true); assert.deepEqual(order, ['voices']);
  assert.equal(await barrier.drain(), true); assert.deepEqual(order, ['voices', 'voices', 'composition', 'spatial']); assert.equal(identity, 'spatial'); barrier.end();
});
test('failed first recipe prevents later mutation and capture', async () => {
  const barrier = new EditBarrier(); let spatial = false;
  barrier.registerFinalizer(async () => false, 0); barrier.registerFinalizer(async () => { spatial = true; return true; }, 1);
  barrier.begin(); assert.equal(await barrier.drain(), false); assert.equal(spatial, false); barrier.end();
});
test('spatial replies reject malformed settings, incomplete preparation and fabricated note accounting', async () => {
  let payload = { settings, preparation: prep, routedNotes: 2, unassignedNotes: 0, wetActive: true };
  const client = new HostClient({ request: async request => ({ requestId: request.requestId, ok: true, result: request.method === 'getCapabilities' ? caps : payload }) }); await client.connect();
  const call = () => client.call('setSpatial', { settings, preparationId: 'audio-1', generation: '1' });
  assert.equal((await call()).ok, true);
  for (const patch of [{ settings: { ...settings, amount: 2 } }, { preparation: null }, { preparation: {} }, { routedNotes: -1 }, { unassignedNotes: 1.5 }, { wetActive: 1 }]) {
    payload = { settings, preparation: prep, ...patch }; assert.equal((await call()).ok, false);
  }
  payload = { settings, preparation: null }; assert.equal((await client.call('getSpatial', {})).ok, true);
});

test('fresh preparation rebases only across unchanged own recipe and the same source piece', () => {
  const previous = { settings, preparation: { ...prep, session: { id: 'same-source' } } };
  assert.equal(recipeStillCurrent({ ...previous, preparation: { ...previous.preparation, preparationId: 'audio-new' } }, previous), true);
  assert.equal(recipeStillCurrent({ ...previous, settings: { ...settings, amount: .8 } }, previous), false);
  assert.equal(recipeStillCurrent({ ...previous, preparation: { ...previous.preparation, session: { id: 'other-source' } } }, previous), false);
  assert.equal(recipeStillCurrent({ settings, preparation: prep }, { settings, preparation: prep }), false);
});
