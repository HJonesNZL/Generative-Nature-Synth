import test from 'node:test';
import assert from 'node:assert/strict';
import { HostClient } from '../src/host/client.ts';
globalThis.window = { setTimeout, clearTimeout };
const valid = { enabled: false, status: 'off', received: 0, admitted: 0, dropped: 0, skippedRevisions: 0, error: '' };
async function fixture(supported = true) {
  const state = { reply: valid, calls: [] };
  const client = new HostClient({ request: async request => {
    state.calls.push(request);
    return { ok: true, requestId: request.requestId, result: request.method === 'getCapabilities'
      ? { protocolVersion: 1, implementation: 'native', supportedMethods: ['getCapabilities', ...(supported ? ['getLiveState', 'setLiveEnabled'] : [])], audioAvailable: true, maxEventBatch: 500, maxVisibleMarkers: 400 }
      : state.reply };
  } });
  assert.equal((await client.connect()).ok, true);
  return { client, state };
}
test('live listening accepts explicit off, listening, overload and failure status', async () => {
  const { client, state } = await fixture();
  for (const reply of [valid, { ...valid, enabled: true, status: 'listening', received: 12, admitted: 10, dropped: 2 }, { ...valid, status: 'error', error: 'Device lost' }]) {
    state.reply = reply;
    assert.deepEqual(await client.call('getLiveState', {}), { ok: true, value: reply });
  }
  state.reply = { ...valid, enabled: true, status: 'listening' };
  assert.equal((await client.call('setLiveEnabled', { enabled: true })).ok, true);
  assert.deepEqual(state.calls.at(-1).args, { enabled: true }, 'control sends no animal event payload');
});
test('live listening rejects malformed, contradictory and unbounded host status', async () => {
  const { client, state } = await fixture();
  for (const reply of [null, {}, { ...valid, enabled: 'yes' }, { ...valid, status: 'playing' }, { ...valid, status: 'listening' }, { ...valid, enabled: true }, { ...valid, dropped: -1 }, { ...valid, received: 0.5 }, { ...valid, admitted: Number.MAX_SAFE_INTEGER + 1 }, { ...valid, error: null }]) {
    state.reply = reply;
    const result = await client.call('getLiveState', {});
    assert.equal(result.ok, false);
    assert.equal(result.error.message, 'Malformed host result');
  }
});
test('older hosts never receive unsupported live listening mutations', async () => {
  const { client, state } = await fixture(false);
  assert.equal((await client.call('setLiveEnabled', { enabled: true })).error.code, 'unsupported');
  assert.equal(state.calls.length, 1);
});
