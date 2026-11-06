import test from 'node:test';
import assert from 'node:assert/strict';
import { EditBarrier } from '../src/app/editBarrier.ts';
import { HostClient } from '../src/host/client.ts';
globalThis.window = { setTimeout, clearTimeout };
const settings = { enabled: true, tempoBpm: 117, tonicPitchClass: 8, scale: 'naturalMinor', density: 2, variation: .25, seed: 4294967295, quantize: false };
const preparation = { preparationId: 'audio-1', generation: '2', durationUs: '100', eventCount: 2, peakEventsPerSecond: 1 };
const caps = { protocolVersion: 1, implementation: 'native', supportedMethods: ['getCapabilities', 'getComposition', 'setComposition'], audioAvailable: false, maxEventBatch: 500, maxVisibleMarkers: 500 };
const deferred = () => { let resolve; const promise = new Promise(done => { resolve = done; }); return { promise, resolve }; };
test('composition finalization follows retiring voice gestures and acknowledged mutations before capture', async () => {
  const barrier = new EditBarrier(), voice = deferred(), composition = deferred(), order = [];
  barrier.register(async () => { await voice.promise; order.push('voice-end'); return true; });
  barrier.registerFinalizer(async () => { order.push('composition-submit'); await composition.promise; order.push('composition-ack'); return true; });
  barrier.begin(); const captured = barrier.drain().then(ok => { assert.equal(ok, true); order.push('capture'); });
  await new Promise(setImmediate); assert.deepEqual(order, []);
  voice.resolve(); await new Promise(setImmediate); assert.deepEqual(order, ['voice-end', 'composition-submit']);
  composition.resolve(); await captured; assert.deepEqual(order, ['voice-end', 'composition-submit', 'composition-ack', 'capture']); barrier.end();
});
test('failed composition acknowledgement prevents capture', async () => {
  const barrier = new EditBarrier(); barrier.registerFinalizer(async () => false); barrier.begin(); assert.equal(await barrier.drain(), false); barrier.end();
});
test('composition host replies require complete bounded canonical settings and preparation', async () => {
  let payload = { settings, preparation };
  const client = new HostClient({ request: async request => ({ requestId: request.requestId, ok: true, result: request.method === 'getCapabilities' ? caps : payload }) }); await client.connect();
  const call = () => client.call('setComposition', { settings, preparationId: 'audio-0', generation: '1' });
  assert.equal((await call()).ok, true);
  for (const patch of [{ tempoBpm: 39 }, { tempoBpm: 90.5 }, { seed: 4294967296 }, { density: 'active' }, { tonicPitchClass: -1 }, { variation: NaN }, { scale: 'unknown' }, { enabled: 1 }, { extra: true }]) {
    payload = { settings: { ...settings, ...patch }, preparation }; assert.equal((await call()).ok, false);
  }
  payload = { settings, preparation: null }; assert.equal((await call()).ok, false); assert.equal((await client.call('getComposition', {})).ok, true);
  payload = { settings, preparation, generatedNotes: -1 }; assert.equal((await call()).ok, false);
  for (const suppressedSourceEvents of [-1, .5, 50001, '1']) {
    payload = { settings, preparation, suppressedSourceEvents }; assert.equal((await call()).ok, false);
  }
  payload = { settings, preparation, generatedNotes: 4, suppressed: 2, suppressedSourceEvents: 1 }; assert.equal((await call()).ok, true);
});
