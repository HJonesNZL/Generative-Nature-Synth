import test from 'node:test';
import assert from 'node:assert/strict';
import { GestureQueue } from '../src/app/gestureQueue.ts';
import { HostClient } from '../src/host/client.ts';
globalThis.window = { setTimeout, clearTimeout };
const deferred = () => { let resolve; const promise = new Promise(done => { resolve = done; }); return { promise, resolve }; };
const ok = value => ({ ok: true, value });

test('flush waits for coalesced changes and closes a held gesture before persistence', async () => {
  const requests = [], gates = [];
  const queue = new GestureQueue((value, gesture) => { const gate = deferred(); gates.push(gate); requests.push({ value, gesture }); return gate.promise; }, () => {});
  queue.push('begin', .5); queue.push('change', .6); queue.push('change', .7);
  let finished = false; const flushed = queue.flush().then(result => { finished = true; return result; });
  assert.deepEqual(requests, [{ value: .5, gesture: 'begin' }]);
  gates[0].resolve(ok({ changeRevision: '9007199254740993', normalizedValue: .5 })); await new Promise(setImmediate);
  assert.deepEqual(requests[1], { value: .7, gesture: 'change' }); assert.equal(finished, false);
  gates[1].resolve(ok({ changeRevision: '9007199254740994', normalizedValue: .7083333333333334 })); await new Promise(setImmediate);
  assert.deepEqual(requests[2], { value: .7, gesture: 'end' }); assert.equal(finished, false);
  gates[2].resolve(ok({ changeRevision: '9007199254740995', normalizedValue: .7083333333333334 }));
  assert.equal(await flushed, true); assert.equal(queue.pending, false);
  assert.equal(queue.isOwnEcho('9007199254740994'), true);
});

test('a rejected change restores the acknowledged value in its closing end and blocks save', async () => {
  const requests = [];
  const queue = new GestureQueue(async (value, gesture) => {
    requests.push({ value, gesture });
    return gesture === 'change' ? { ok: false, error: { code: 'unavailable', message: 'rejected change' } } : ok({ normalizedValue: value });
  }, () => {});
  queue.push('begin', .5); queue.push('change', .9); queue.push('end', .9);
  assert.equal(await queue.flush(), false);
  assert.deepEqual(requests.at(-1), { value: .5, gesture: 'end' });
  queue.push('begin', .5); queue.push('end', .5);
  assert.equal(await queue.flush(), true);
});

test('throws settle flush and do not leave waiters pending', async () => {
  const queue = new GestureQueue(async () => { throw Error('worker gone'); }, () => {});
  queue.push('begin', .5);
  assert.equal(await queue.flush(), false); assert.equal(queue.pending, false);
});

const caps = { protocolVersion: 1, implementation: 'native', supportedMethods: ['getCapabilities', 'getVoice', 'getVoiceLibrary', 'setVoiceParameter'], audioAvailable: false, maxEventBatch: 500, maxVisibleMarkers: 500 };
const voice = { id: 'species:a', name: 'A', scope: 'species', targetId: 'a', inheritsFromId: null, type: 'struck', values: { tuning: .5 }, savedRevision: '1', changeRevision: '3', dirty: false, overriddenParameterIds: [] };
test('host rejects malformed voice metadata, wrong identity and invalid canonical acknowledgements', async () => {
  let payload;
  const client = new HostClient({ request: async request => ({ requestId: request.requestId, ok: true, result: request.method === 'getCapabilities' ? caps : payload }) });
  await client.connect();
  for (const invalid of [{ ...voice, id: 'species:b' }, { ...voice, values: { tuning: 2 } }, { ...voice, dirty: 'yes' }, { ...voice, savedRevision: '-1' }, { ...voice, changeRevision: undefined }]) {
    payload = invalid; assert.equal((await client.call('getVoice', { voiceId: 'species:a' })).ok, false);
  }
  payload = voice; assert.equal((await client.call('getVoice', { voiceId: 'species:a' })).ok, true);
  for (const invalid of [{ normalizedValue: NaN, changeRevision: '1', dirty: true }, { normalizedValue: .5, changeRevision: '1' }]) {
    payload = invalid; assert.equal((await client.call('setVoiceParameter', { voiceId: 'species:a', parameterId: 'tuning', gesture: 'end', normalizedValue: .5 })).ok, false);
  }
  payload = { normalizedValue: .7083333333333334, changeRevision: '9007199254740993', dirty: true };
  assert.equal((await client.call('setVoiceParameter', { voiceId: 'species:a', parameterId: 'tuning', gesture: 'end', normalizedValue: .7 })).value.normalizedValue, payload.normalizedValue);
  payload = { entries: [], parameters: [{ id: 'attack', label: 'Attack', unit: 's', min: 0, max: 1, defaultValue: .1, curve: 'log', proposal: false }] };
  assert.equal((await client.call('getVoiceLibrary', {})).ok, false);
});


test('native voice IDs include the full 256-character species ID plus prefix', async () => {
  const speciesId = 'a'.repeat(256), voiceId = `species:${speciesId}`;
  const fullVoice = { ...voice, id: voiceId, targetId: speciesId };
  let payload = fullVoice;
  const client = new HostClient({ request: async request => ({ requestId: request.requestId, ok: true, result: request.method === 'getCapabilities' ? caps : payload }) });
  await client.connect();
  assert.equal((await client.call('getVoice', { voiceId })).ok, true);
  payload = { entries: [{ groupId: 'native', groupName: 'Voices', groupVoiceId: 'default', species: [{ speciesId, commonName: 'Long ID', scientificName: '', voiceId, individuals: [] }] }], parameters: [] };
  assert.equal((await client.call('getVoiceLibrary', {})).ok, true);
  payload = { ...fullVoice, id: voiceId + 'x' };
  assert.equal((await client.call('getVoice', { voiceId: voiceId + 'x' })).ok, false);
});


test('failed gestures discard later queued gestures without sending unmatched ends', async () => {
  const requests = [];
  const queue = new GestureQueue(async (value, gesture) => {
    requests.push({ value, gesture });
    return gesture === 'change' ? { ok: false, error: { code: 'unavailable', message: 'rejected' } } : ok({ normalizedValue: value, changeRevision: '100' });
  }, () => {});
  queue.push('begin', .5); queue.push('change', .9); queue.push('end', .9);
  queue.push('begin', .9); queue.push('change', 1); queue.push('end', 1);
  assert.equal(await queue.flush(), false);
  assert.deepEqual(requests.map(request => request.gesture), ['begin', 'change', 'end']);
  assert.equal(requests.at(-1).value, .5);
  queue.resetError(); assert.equal(queue.isOwnEcho('1'), false);
});
