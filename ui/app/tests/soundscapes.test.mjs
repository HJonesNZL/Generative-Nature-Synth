import test from 'node:test';
import assert from 'node:assert/strict';
import { EditBarrier } from '../src/app/editBarrier.ts';
import { GestureQueue } from '../src/app/gestureQueue.ts';
import { HostClient } from '../src/host/client.ts';
globalThis.window = { setTimeout, clearTimeout };
const id = 'a'.repeat(32), jobId = 'b'.repeat(32);
const scene = { id, name: 'Captured forest', valid: true, contentVerified: true, error: null, contentHash: 'c'.repeat(64), sourceSessionId: 'source', eventCount: 2, durationUs: '1000000', basis: 'observed', speedNumerator: '60', speedDenominator: '1' };
const caps = { protocolVersion: 1, implementation: 'native', supportedMethods: ['getCapabilities', 'listSoundscapes', 'saveSoundscape', 'recallSoundscape', 'startSoundscapeRender', 'getRenderJob'], audioAvailable: false, maxEventBatch: 500, maxVisibleMarkers: 500 };
const deferred = () => { let resolve; const promise = new Promise(done => { resolve = done; }); return { promise, resolve }; };
test('capture waits for held and retiring edit queues plus pending type changes', async () => {
  const barrier = new EditBarrier(), gates = [], sent = [];
  const queue = new GestureQueue((value, gesture) => { const gate = deferred(); gates.push(gate); sent.push(gesture); return gate.promise; }, () => {});
  const retire = barrier.register(() => queue.flush());
  queue.push('begin', .3); queue.push('change', .8); retire();
  const typeGate = deferred(); barrier.track(typeGate.promise);
  assert.equal(barrier.begin(), true); assert.equal(barrier.begin(), false);
  let captured = false; const capture = barrier.drain().then(ok => { captured = ok; });
  gates[0].resolve({ ok: true, value: {} }); await new Promise(setImmediate);
  gates[1].resolve({ ok: true, value: {} }); await new Promise(setImmediate);
  assert.deepEqual(sent, ['begin', 'change', 'end']); assert.equal(captured, false);
  gates[2].resolve({ ok: true, value: {} }); await new Promise(setImmediate); assert.equal(captured, false);
  typeGate.resolve({ ok: true }); await capture; assert.equal(captured, true); barrier.end();
});
test('capture blocks failed edits while explicit recall may discard their drained drafts', async () => {
  const barrier = new EditBarrier(); barrier.register(async () => false); barrier.begin();
  assert.equal(await barrier.drain(), false); assert.equal(await barrier.drain(false), true); barrier.end();
});
test('scene replies reject wrong recall identity, incomplete data and unsafe artifact names', async () => {
  let payload;
  const client = new HostClient({ request: async request => ({ requestId: request.requestId, ok: true, result: request.method === 'getCapabilities' ? caps : payload }) });
  await client.connect();
  for (const invalid of [{ ...scene, id: '../x' }, { ...scene, speedDenominator: '0' }, { ...scene, contentHash: 'broken' }, { ...scene, eventCount: -1 }]) {
    payload = invalid; assert.equal((await client.call('saveSoundscape', { name: 'x' })).ok, false);
  }
  payload = scene; assert.equal((await client.call('saveSoundscape', { name: 'x' })).ok, true);
  payload = { items: [scene], limited: true, unmanagedFiles: 2 }; assert.equal((await client.call('listSoundscapes', { limit: 100 })).ok, true);
  for (const unmanagedFiles of [-1, 101, .5, '2']) {
    payload = { items: [scene], limited: true, unmanagedFiles }; assert.equal((await client.call('listSoundscapes', { limit: 100 })).ok, false);
  }
  const preparation = { preparationId: 'prep', generation: '1', durationUs: '1000000', eventCount: 2, peakEventsPerSecond: 2, session: { id: `scene:${id}`, name: scene.name, state: "frozen", origin: "archive_snapshot", eventCount: 2, captureTruncated: false, observedBoundsUs: ["1000000", "2000000"], arrivalBoundsUs: ["1100000", "2100000"] } };
  payload = { soundscape: scene, preparation }; assert.equal((await client.call('recallSoundscape', { soundscapeId: id })).ok, true);
  payload = { soundscape: scene, preparation: { ...preparation, session: { id: 'wrong' } } }; assert.equal((await client.call('recallSoundscape', { soundscapeId: id })).ok, false);
  const job = { jobId, state: 'complete', progress: 1, message: null, artifactId: jobId, filename: 'saved.wav', soundscapeId: id };
  for (const bad of [{ ...job, filename: '../saved.wav' }, { ...job, artifactId: '../path' }, { ...job, progress: 2 }, { ...job, soundscapeId: 'd'.repeat(32) }]) {
    payload = bad; assert.equal((await client.call('startSoundscapeRender', { soundscapeId: id })).ok, false);
  }
  payload = job; assert.equal((await client.call('startSoundscapeRender', { soundscapeId: id })).ok, true);
});
