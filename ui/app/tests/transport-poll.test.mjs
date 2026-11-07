import test from 'node:test';
import assert from 'node:assert/strict';
import { transportPollUpdate } from '../src/app/transportPoll.ts';

const live = { preparationId: null, generation: '0', state: 'stopped', positionUs: '0', durationUs: null };
const native = { preparationId: 'audio-1', generation: '1', state: 'playing', positionUs: '0', durationUs: '0' };
const failed = { ok: false, error: { code: 'internal', message: 'Malformed host result' } };
const healthy = { ok: true, value: native };

test('live poll recovery clears its transient fault without adopting a replay preparation', () => {
  const failedView = { ...live, ...transportPollUpdate(live, failed) };
  assert.equal(failedView.pollError, 'Malformed host result');
  assert.deepEqual({ ...failedView, ...transportPollUpdate(failedView, healthy) }, { ...live, pollError: null });
});

test('healthy polls preserve action and preparation faults for live and replay', () => {
  for (const base of [live, native]) {
    const expected = { ...base, error: 'Preparation failed', pollError: 'Timed out' };
    const recovered = { ...expected, ...transportPollUpdate(expected, healthy) };
    assert.equal(recovered.error, 'Preparation failed');
    assert.equal(recovered.pollError, null);
  }
});

test('replay polls reject stale generations and surface preparation replacement', () => {
  const expected = { ...native, generation: '2' };
  assert.equal(transportPollUpdate(expected, healthy), null);
  assert.deepEqual(transportPollUpdate(expected, { ok: true, value: { ...native, preparationId: 'audio-2' } }), {
    preparationId: null, state: 'stopped', pollError: null,
    error: 'Playback preparation changed. Select the saved piece again.',
  });
});

test('unchanged healthy live polls and repeated poll failures need no dispatch', () => {
  assert.equal(transportPollUpdate(live, healthy), null);
  assert.equal(transportPollUpdate({ ...live, pollError: null }, healthy), null);
  assert.equal(transportPollUpdate({ ...live, pollError: failed.error.message }, failed), null);
});
