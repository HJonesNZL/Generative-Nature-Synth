import test from 'node:test';
import assert from 'node:assert/strict';
import { HostClient } from '../src/host/client.ts';
import { EventStore } from '../src/app/eventStore.ts';

globalThis.window = { setTimeout, clearTimeout,
  requestAnimationFrame: (fn) => setTimeout(fn, 0), cancelAnimationFrame: clearTimeout };
globalThis.document = { hidden: false };
const caps = { protocolVersion: 1, implementation: 'native', supportedMethods: ['getCapabilities', 'listSessions'],
  audioAvailable: false, maxEventBatch: 500, maxVisibleMarkers: 500 };
const ok = (request, result) => ({ requestId: request.requestId, ok: true, result });

test('capability gate prevents sending unsupported mutations', async () => {
  const calls = [];
  const client = new HostClient({ request: async (request) => { calls.push(request.method); return ok(request, caps); } });
  assert.equal((await client.connect()).ok, true);
  const denied = await client.call('setVoiceParameter', {});
  assert.equal(denied.error.code, 'unsupported');
  assert.deepEqual(calls, ['getCapabilities']);
});

test('invalid capabilities and mismatched replies fail closed', async () => {
  for (const value of [null, {}, { ...caps, maxEventBatch: 0 }, { ...caps, supportedMethods: [12] }, { ...caps, libraryAvailable: "false" }, { ...caps, libraryAvailable: null }]) {
    const client = new HostClient({ request: async (request) => ok(request, value) });
    assert.equal((await client.connect()).ok, false);
    assert.equal(client.supports('listSessions'), false);
  }
  const client = new HostClient({ request: async () => ({ requestId: 'wrong', ok: true, result: caps }) });
  assert.equal((await client.connect()).error.code, 'internal');
});

test('abort settles promptly even when the host ignores its signal', async () => {
  const client = new HostClient({ request: async (request) => request.method === 'getCapabilities' ? ok(request, caps) : new Promise(() => {}) });
  await client.connect();
  const controller = new AbortController();
  const pending = client.call('listSessions', { limit: 10 }, { signal: controller.signal, timeoutMs: 5000 });
  controller.abort();
  const result = await Promise.race([pending, new Promise((resolve) => setTimeout(() => resolve('not settled'), 50))]);
  assert.notEqual(result, 'not settled'); assert.equal(result.error.code, 'aborted');
});

test('timeout remains distinct from user cancellation', async () => {
  const client = new HostClient({ request: async (request) => request.method === 'getCapabilities' ? ok(request, caps) : new Promise(() => {}) });
  await client.connect();
  assert.equal((await client.call('listSessions', { limit: 10 }, { timeoutMs: 5 })).error.code, 'timeout');
});

const event = (id, revisionSequence = '1') => ({ id, revisionSequence, position: { latitude: -41, longitude: 174 } });
test('duplicate and older revisions cannot re-pulse or replace newer records', () => {
  const store = new EventStore('test');
  store.upsert([{ event: event('one', '9007199254740993'), pulse: true }]);
  store.upsert([{ event: event('one', '9007199254740993'), pulse: true }, { event: event('one', '2'), pulse: true }]);
  assert.equal(store.activePulses(10000).length, 1);
  assert.equal(store.get('one').revisionSequence, '9007199254740993');
  store.upsert([{ event: event('one', '9007199254740994'), pulse: false }]);
  assert.equal(store.activePulses(10000).length, 1);
  assert.equal(store.get('one').revisionSequence, '9007199254740994');
  store.dispose();
});

test('bounded buffers explicitly count display omissions and reset across material', () => {
  const store = new EventStore('test');
  store.upsert(Array.from({ length: 2003 }, (_, i) => ({ event: event(String(i)), pulse: false })));
  assert.equal(store.size, 2000); assert.equal(store.feed.displayOmitted, 3);
  store.clear(); assert.equal(store.size, 0); assert.equal(store.feed.displayOmitted, 0); store.dispose();
});

test('late musical outcomes cannot cross a material reset', async () => {
  const store = new EventStore('test'); let finish;
  const client = { supports: () => true, call: () => new Promise((resolve) => { finish = resolve; }) };
  store.upsert([{ event: event('one'), pulse: false }]); store.requestOutcomes(client, ['one']);
  await new Promise((resolve) => setTimeout(resolve, 135));
  store.clear(); store.upsert([{ event: event('one'), pulse: false }]);
  finish({ ok: true, value: { one: { kind: 'sounded' } } });
  await new Promise((resolve) => setTimeout(resolve, 0));
  assert.equal(store.outcome('one'), undefined); store.dispose();
});


test('playback payload validation rejects malformed native timelines and audio states', async () => {
  const methods = ['getCapabilities', 'getTransport', 'setTransport', 'getAudioState', 'prepareReplay'];
  let payload;
  const client = new HostClient({ request: async request => ok(request, request.method === 'getCapabilities' ? { ...caps, supportedMethods: methods } : payload) });
  await client.connect();
  const timeline = { preparationId: 'prep', generation: '1', state: 'stopped', positionUs: '0', durationUs: '1000000' };
  for (const invalid of [{ ...timeline, positionUs: '-1' }, { ...timeline, durationUs: 'NaN' }, { ...timeline, positionUs: '1000001' }, { ...timeline, generation: '9223372036854775808' }, { ...timeline, state: 'fake' }]) {
    payload = invalid;
    assert.equal((await client.call('getTransport', {})).error.code, 'internal');
  }
  payload = timeline;
  assert.equal((await client.call('getTransport', {})).ok, true);
  const before = client.playbackRevision;
  await client.call('setTransport', { action: 'panic' });
  assert.equal(client.playbackRevision, before + 1);
  payload = { status: 'disabled', deviceName: null, masterGainDb: -18, peakDbfs: null, meterIsSimulated: false, clipping: false, activeSynthVoices: null, maxSynthVoices: 8 };
  assert.equal((await client.call('getAudioState', {})).ok, true);
  payload = { ...payload, masterGainDb: NaN };
  assert.equal((await client.call('getAudioState', {})).error.code, 'internal');
});
