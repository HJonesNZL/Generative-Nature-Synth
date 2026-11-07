import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const script = readFileSync(new URL('../native/src/standalone_bridge.hpp', import.meta.url), 'utf8').split('R"ATLASJS(')[1].split(')ATLASJS"')[0];
function bridge() {
 const listeners = new Map(), sent = [], timers = new Map(), windowEvents = new Map(), documentEvents = new Map(); let timerId = 0;
 const backend = {addEventListener: (key, listener) => listeners.set(key, listener), emitEvent: (key, value) => sent.push({key, value})};
 const window = {__JUCE__: {backend}, addEventListener: (key, listener) => windowEvents.set(key, listener)};
 vm.runInNewContext(script, {window, document: {addEventListener: (key, listener) => documentEvents.set(key, listener)}, TextEncoder, setTimeout: fn => {timers.set(++timerId, fn); return timerId;}, clearTimeout: id => timers.delete(id)});
 return {host: window.atlasHost, sent, reply: event => listeners.get('atlasReply')(event), timers, windowEvents, documentEvents};
}
const request = requestId => ({protocolVersion: 1, requestId, method: 'getCapabilities', args: {}});
test('embedded bridge bounds requests, matches replies, and retires abort/timeout/page state', async () => {
 const b = bridge(); const first = b.host.request(request('one')); const token = b.sent[0].value.token;
 b.reply({token: token + 10, json: '{}'}); b.reply({token, json: JSON.stringify({requestId: 'one', ok: true, result: {}})});
 assert.equal((await first).ok, true); assert.equal(b.timers.size, 0);
 const wrong = b.host.request(request('two')); b.reply({token: b.sent.at(-1).value.token, json: JSON.stringify({requestId: 'other', ok: true})}); assert.equal((await wrong).error.code, 'internal');
 const pending = Array.from({length: 16}, (_, i) => b.host.request(request(`p${i}`)));
 assert.equal((await b.host.request(request('overflow'))).error.code, 'unavailable');
 b.windowEvents.get('pagehide')(); assert.ok((await Promise.all(pending)).every(reply => reply.error.code === 'unavailable')); assert.equal(b.timers.size, 0);
 const control = new AbortController(); const aborted = b.host.request(request('abort'), control.signal); control.abort(); assert.equal((await aborted).error.code, 'aborted');
 const timeout = b.host.request(request('timeout')); [...b.timers.values()][0](); assert.equal((await timeout).error.code, 'timeout');
 assert.equal((await b.host.request({...request('large'), args: {text: 'x'.repeat(16385)}})).error.code, 'invalid_request');
 assert.equal((await b.host.request({...request('unicode'), args: {text: '魚'.repeat(6000)}})).error.code, 'invalid_request');
});
test('WAV link uses a native export event only for opaque artifact ids', () => {
 const b = bridge(); let prevented = false;
 b.documentEvents.get('click')({target: {closest: selector => selector === 'a[data-artifact-id]' ? ({dataset: {artifactId: 'a'.repeat(32)}}) : null}, preventDefault: () => {prevented = true;}});
 assert.equal(prevented, true); assert.equal(b.sent[0].key, 'atlasExport'); assert.equal(b.sent[0].value.artifactId, 'a'.repeat(32));
 b.documentEvents.get('click')({target: {closest: selector => selector === 'a[data-artifact-id]' ? ({dataset: {artifactId: '../../private'}}) : null}, preventDefault() {}}); assert.equal(b.sent.length, 1);
});
test('verified scene export links route only opaque scene identities', () => {
 const b = bridge(); let prevented = 0;
 const click = sceneId => b.documentEvents.get('click')({target: {closest: selector => selector.includes('scene') ? {dataset: {sceneId}} : null}, preventDefault() {prevented++;}});
 click('c'.repeat(32)); click('../private');
 assert.equal(prevented, 2);
 assert.deepEqual(JSON.parse(JSON.stringify(b.sent)), [{key: 'atlasSceneExport', value: {sceneId: 'c'.repeat(32)}}]);
});
test('native reveal links admit only opaque artifact identities', () => {
 const b = bridge();
 const click = artifactId => b.documentEvents.get('click')({target: {closest: selector => selector === '[data-reveal-artifact-id]' ? {dataset: {revealArtifactId: artifactId}} : null}, preventDefault() {}});
 click('d'.repeat(32)); click('../../private');
 assert.deepEqual(JSON.parse(JSON.stringify(b.sent)), [{key: 'atlasRevealExport', value: {artifactId: 'd'.repeat(32)}}]);
});
