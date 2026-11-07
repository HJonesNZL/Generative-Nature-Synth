import test from 'node:test';
import assert from 'node:assert/strict';
import { HostClient } from '../src/host/client.ts';

globalThis.window = { setTimeout, clearTimeout };
const methods = ['listSessionsPage', 'getLiveSources', 'setLiveSources'];
const caps = { protocolVersion: 1, implementation: 'native', supportedMethods: ['getCapabilities', ...methods], audioAvailable: false, libraryAvailable: true, maxEventBatch: 500, maxVisibleMarkers: 400 };
const ok = (request, result) => ({ requestId: request.requestId, ok: true, result });
const session = (id = 'piece-1') => ({ id, name: 'Synthetic saved piece', state: 'frozen', origin: 'archive_snapshot', eventCount: 1, captureTruncated: false, observedBoundsUs: ['-10', '10'], arrivalBoundsUs: null });
const page = () => ({ items: [session()], nextCursor: 'bounded-opaque-next-page', revision: '7', sourceId: '0123456789abcdef0123456789abcdef' });
const source = (id, enabled = false) => ({ id, enabled, polling: false, lastAttemptUtc: null, lastSuccessUtc: null, lastObservationUtc: null, records: '0', error: null, complete: false, gap: false, nextPollSeconds: enabled ? 60 : 0 });
const live = () => ({ collecting: true, libraryActive: true, sources: [source('birdweather', true), source('nzsharks')] });
async function harness() {
  const state = { payload: null, override: null, calls: [] };
  const client = new HostClient({ request: async request => {
    state.calls.push(request);
    if (request.method === 'getCapabilities') return ok(request, caps);
    return state.override ? state.override(request) : ok(request, state.payload);
  } });
  assert.equal((await client.connect()).ok, true);
  return { client, state };
}
async function rejects(client, state, method, args, payload, label) {
  state.payload = payload;
  const reply = await client.call(method, args);
  assert.equal(reply.ok, false, label);
  assert.equal(reply.error.code, 'internal', label);
  assert.equal(reply.error.message, 'Malformed host result', label);
}

test('session-page boundary accepts bounded pages, final pages and exact 64-bit bounds', async () => {
  const { client, state } = await harness();
  for (const payload of [page(), { ...page(), nextCursor: null }, { ...page(), items: [], nextCursor: null }, { ...page(), items: Array.from({ length: 100 }, (_, i) => session(`piece-${i}`)), revision: '9223372036854775807', nextCursor: 'x'.repeat(2048) }, { ...page(), items: [{ ...session(), observedBoundsUs: ['-9223372036854775808', '9223372036854775807'] }] }]) {
    state.payload = payload;
    assert.deepEqual(await client.call('listSessionsPage', { limit: 100, cursor: null }), { ok: true, value: payload });
  }
});

test('session-page boundary rejects invalid source identity, revision, cursor and collection shape', async () => {
  const { client, state } = await harness();
  const cases = [null, [], {}, { ...page(), items: null }, { ...page(), items: Array.from({ length: 101 }, (_, i) => session(`piece-${i}`)) }, { ...page(), sourceId: 'owner-path-is-not-an-opaque-library-id' }, { ...page(), sourceId: 'A'.repeat(32) }, { ...page(), revision: 7 }, { ...page(), revision: '07' }, { ...page(), revision: '-1' }, { ...page(), revision: '9223372036854775808' }, { ...page(), nextCursor: '' }, { ...page(), nextCursor: 'x'.repeat(2049) }, { ...page(), nextCursor: 1 }, { ...page(), items: [], nextCursor: 'cannot-advance-an-empty-page' }];
  for (const [index, payload] of cases.entries()) await rejects(client, state, 'listSessionsPage', { limit: 100, cursor: null }, payload, `invalid page case ${index}`);
});

test('session-page boundary rejects duplicate identities and malformed saved metadata', async () => {
  const { client, state } = await harness();
  await rejects(client, state, 'listSessionsPage', { limit: 100, cursor: null }, { ...page(), items: [session(), session()] }, 'duplicate session identity');
  const invalid = [{ id: '' }, { id: null }, { name: 'x'.repeat(513) }, { state: 'ready' }, { origin: 'unverified_stream' }, { eventCount: -1 }, { eventCount: 0.5 }, { eventCount: Infinity }, { captureTruncated: 'false' }, { observedBoundsUs: ['10', '-10'] }, { observedBoundsUs: ['-0', '1'] }, { observedBoundsUs: ['-9223372036854775809', '1'] }, { arrivalBoundsUs: ['0', '9223372036854775808'] }];
  for (const [index, patch] of invalid.entries()) await rejects(client, state, 'listSessionsPage', { limit: 100, cursor: null }, { ...page(), items: [{ ...session(), ...patch }] }, `invalid session case ${index}`);
});

test('session-page replies cannot exceed the requested page size', async () => {
  const { client, state } = await harness();
  state.payload = { ...page(), items: [session('one'), session('two')] };
  assert.equal((await client.call('listSessionsPage', { limit: 2, cursor: null })).ok, true);
  await rejects(client, state, 'listSessionsPage', { limit: 1, cursor: null }, state.payload, 'reply exceeds requested one-row page');
});

test('live-source boundary accepts paused state, nullable observations and full retry interval', async () => {
  const { client, state } = await harness();
  const stamp = '2026-11-07T00:00:00.123456Z';
  const active = live();
  Object.assign(active.sources[0], { lastAttemptUtc: stamp, lastSuccessUtc: stamp, lastObservationUtc: stamp, records: '9223372036854775807', error: 'rate_limited', nextPollSeconds: 86400, complete: true, gap: true });
  for (const method of ['getLiveSources', 'setLiveSources']) {
    for (const payload of [active, { ...active, sources: [...active.sources].reverse(), libraryChanged: false }, { collecting: false, libraryActive: false, libraryChanged: true, sources: [source('birdweather'), source('nzsharks')] }]) {
      state.payload = payload;
      const args = method === 'getLiveSources' ? {} : { sourceIds: payload.sources.filter(source => source.enabled).map(source => source.id) };
      assert.deepEqual(await client.call(method, args), { ok: true, value: payload });
    }
  }
});

test('live-source boundary requires known unique source IDs and coherent collection state', async () => {
  const { client, state } = await harness();
  const cases = [null, {}, { ...live(), sources: [] }, { ...live(), sources: [source('birdweather', true)] }, { ...live(), sources: [source('birdweather', true), source('birdweather')] }, { ...live(), sources: [source('birdweather', true), source('unknown')] }, { ...live(), sources: [source('birdweather', true), source('nzsharks'), source('extra')] }, { ...live(), collecting: false }, { ...live(), collecting: 'true' }, { ...live(), libraryActive: 'true' }, { ...live(), libraryChanged: null }];
  for (const method of ['getLiveSources', 'setLiveSources']) for (const [index, payload] of cases.entries()) await rejects(client, state, method, method === 'getLiveSources' ? {} : { sourceIds: ['birdweather'] }, payload, `${method} invalid source set ${index}`);
  for (const field of ['enabled', 'polling', 'complete', 'gap']) {
    const payload = live(); payload.sources[0][field] = 'false';
    await rejects(client, state, 'getLiveSources', {}, payload, `${field} must be a boolean`);
  }
});

test('live-source timestamps must be nullable parseable UTC timestamps', async () => {
  const { client, state } = await harness();
  for (const field of ['lastAttemptUtc', 'lastSuccessUtc', 'lastObservationUtc']) {
    for (const timestamp of [undefined, 0, 'not-a-time', '2026-13-01T00:00:00Z', '2026-11-07T00:00:00', '2026-11-07T00:00:00+13:00', '2026-11-07T00:00:00.' + '1'.repeat(30) + 'Z']) {
      const payload = live(); payload.sources[0][field] = timestamp;
      await rejects(client, state, 'getLiveSources', {}, payload, `invalid ${field}: ${timestamp}`);
    }
  }
});

test('live-source record counters, retry intervals and error codes remain bounded and safe', async () => {
  const { client, state } = await harness();
  for (const [field, values] of Object.entries({ records: [0, '-1', '-0', '01', '1.5', '9223372036854775808'], nextPollSeconds: [-1, 0.5, NaN, Infinity, 86401, '60'], error: ['', 'Unsafe Error', 'https://secret.example/token', 'x'.repeat(101)] })) {
    for (const value of values) {
      const payload = live(); payload.sources[0][field] = value;
      await rejects(client, state, 'getLiveSources', {}, payload, `invalid ${field}: ${value}`);
    }
  }
});

test('setLiveSources receipts must acknowledge the requested source identities', async () => {
  const { client, state } = await harness();
  await rejects(client, state, 'setLiveSources', { sourceIds: [] }, live(), 'pause cannot acknowledge an enabled source');
  await rejects(client, state, 'setLiveSources', { sourceIds: ['nzsharks'] }, live(), 'receipt cannot enable a different source');
  await rejects(client, state, 'setLiveSources', { sourceIds: ['birdweather', 'nzsharks'] }, live(), 'receipt cannot omit a requested source');
  state.payload = { ...live(), sources: [source('nzsharks', true), source('birdweather', true)] };
  assert.equal((await client.call('setLiveSources', { sourceIds: ['birdweather', 'nzsharks'] })).ok, true, 'Receipt order is immaterial');
});

test('new library/live methods correlate reply IDs and preserve explicit host failures', async () => {
  const { client, state } = await harness();
  for (const method of methods) {
    const args = method === 'listSessionsPage' ? { limit: 50, cursor: null } : method === 'setLiveSources' ? { sourceIds: ['birdweather'] } : {};
    const payload = method === 'listSessionsPage' ? page() : live();
    state.override = request => ({ ...ok(request, payload), requestId: 'another-request' });
    assert.equal((await client.call(method, args)).error.code, 'internal');
    state.override = request => ({ requestId: request.requestId, ok: false, error: { code: 'stale_generation', message: 'Synthetic library revision changed' } });
    assert.deepEqual(await client.call(method, args), { ok: false, error: { code: 'stale_generation', message: 'Synthetic library revision changed' } });
  }
});

test('older hosts cannot receive unsupported new library/live requests', async () => {
  const calls = [];
  const client = new HostClient({ request: async request => { calls.push(request.method); return ok(request, { ...caps, supportedMethods: ['getCapabilities'] }); } });
  await client.connect();
  for (const method of methods) assert.equal((await client.call(method, {})).error.code, 'unsupported');
  assert.deepEqual(calls, ['getCapabilities']);
});

test('worldwide collection acknowledges an exclusive bird scope and bounded regional health', async () => {
  const { client, state } = await harness();
  const world = source('birdweather_world', true);
  world.regions = [{id:'asia', lastAttemptUtc:null, lastObservationUtc:null, complete:false, gap:false, error:null}];
  const sources = [source('birdweather'), source('nzsharks'), world, source('inaturalist', true)];
  state.payload = {...live(), sources};
  assert.equal((await client.call('setLiveSources', {sourceIds:['birdweather_world','inaturalist']})).ok, true);
  await rejects(client, state, 'getLiveSources', {}, {...live(), sources:[source('birdweather', true), ...sources.slice(1)]}, 'overlapping bird modes');
  await rejects(client, state, 'getLiveSources', {}, {...live(), sources:[...sources.slice(0,2), {...world, regions:Array(9).fill(world.regions[0])}, sources[3]]}, 'regional status overflow');
});
