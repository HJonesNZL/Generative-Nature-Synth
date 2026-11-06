// Browser-only deferred native fixture. No database, native process or device.
import assert from 'node:assert/strict';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: 'chrome', args: ['--enable-unsafe-swiftshader'] });
const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
const errors = []; page.on('pageerror', error => errors.push(error.message));
await page.addInitScript(() => {
  const make = (id, targetId, name, scope = 'species') => ({ id, targetId, name, scope, inheritsFromId: null, type: 'struck', values: { tuning: .5, attack: Math.log(4)/Math.log(50), decay: Math.log(6.5)/Math.log(30), gain: 1 }, overriddenParameterIds: [], savedRevision: '1', changeRevision: '0', dirty: false });
  const voices = new Map([make('default', 'all_animals', 'Fallback', 'group'), make('species:a', 'a', 'Animal A'), make('species:b', 'b', 'Animal B')].map(v => [v.id, v]));
  const saved = new Map([...voices].map(([id, voice]) => [id, structuredClone(voice)]));
  const f = window.__voiceFixture = { requests: [], hold: false, release: null, reject: false, revision: 0 };
  f.restart = () => { f.revision = 0; for (const [id, voice] of saved) voices.set(id, { ...structuredClone(voice), changeRevision: '0', dirty: false }); };
  f.listeners = new Set();
  f.emit = notification => { for (const listener of f.listeners) listener(notification); };
  const session = { id: 'new-species', name: 'New species piece', state: 'frozen', origin: 'archive_snapshot', eventCount: 0, captureTruncated: false, observedBoundsUs: ['0', '1000000'], arrivalBoundsUs: ['0', '1000000'] };
  const methods = ['prepareReplay', 'getMaterialEvents', 'getCapabilities', 'getVoiceLibrary', 'getVoice', 'setVoiceParameter', 'saveVoice', 'revertVoice', 'getAudioState', 'getCatalog', 'listSessions'];
  window.atlasHost = { subscribe(listener) { f.listeners.add(listener); return () => f.listeners.delete(listener); }, async request(request) {
    f.requests.push(structuredClone(request)); const { method, args } = request; let result;
    if (method === 'getCapabilities') result = { protocolVersion: 1, implementation: 'native', supportedMethods: methods, audioAvailable: false, maxEventBatch: 500, maxVisibleMarkers: 500 };
    else if (method === 'getAudioState') result = { status: 'disabled', deviceName: null, masterGainDb: -18, peakDbfs: null, meterIsSimulated: false, clipping: false, activeSynthVoices: 0, maxSynthVoices: 8 };
    else if (method === 'getCatalog') result = { species: ['a','b'].map(id => ({ id, commonName: `Animal ${id.toUpperCase()}`, scientificName: id, group: 'unclassified' })), stations: [], sources: [] };
    else if (method === 'listSessions') result = [session];
    else if (method === 'getMaterialEvents') result = { total: 0, events: [] };
    else if (method === 'prepareReplay') { await new Promise(resolve => { f.releasePrepare = resolve; }); f.releasePrepare = null; f.prepared = true; voices.set('species:c', make('species:c', 'c', 'Animal C')); result = { preparationId: 'new-species-prepared', generation: '1', durationUs: '1000000', eventCount: 0, peakEventsPerSecond: 0, session }; }
    else if (method === 'getVoiceLibrary') result = { entries: [{ groupId: 'native', groupName: 'Species voices', groupVoiceId: 'default', species: (f.prepared ? ['a','b','c'] : ['a','b']).map(id => ({ speciesId: id, commonName: `Animal ${id.toUpperCase()}`, scientificName: id, voiceId: `species:${id}`, individuals: [] })) }], parameters: [
      { id: 'tuning', label: 'Transpose', unit: 'st', min: -12, max: 12, defaultValue: 0, step: 1, curve: 'linear', proposal: false },
      { id: 'attack', label: 'Attack', unit: 's', min: .002, max: .1, defaultValue: .008, curve: 'log', proposal: false },
      { id: 'decay', label: 'Decay T60', unit: 's', min: .1, max: 3, defaultValue: .65, curve: 'log', proposal: false },
      { id: 'gain', label: 'Gain', unit: 'dB', min: -60, max: 0, defaultValue: 0, curve: 'linear', proposal: false }], types: [{ id: 'struck', label: 'Struck', proposal: false }], maxDefinitions: 513, editingBehavior: 'new_notes' };
    else if (method === 'getVoice') { result = structuredClone(voices.get(args.voiceId)); if (f.holdRead) { f.holdRead = false; await new Promise(resolve => { f.releaseRead = resolve; }); f.releaseRead = null; } }
    else if (method === 'setVoiceParameter') {
      if (f.hold && args.gesture === 'change') { f.hold = false; await new Promise(resolve => { f.release = resolve; }); f.release = null; }
      if (f.reject && args.gesture === 'change') { f.reject = false; return { requestId: request.requestId, ok: false, error: { code: 'unavailable', message: 'Fixture rejected parameter' } }; }
      const v = voices.get(args.voiceId), n = args.parameterId === 'tuning' ? Math.round(args.normalizedValue * 24) / 24 : args.normalizedValue;
      if (args.gesture !== 'begin') v.values[args.parameterId] = n; v.dirty = JSON.stringify(v.values) !== JSON.stringify(saved.get(v.id).values); v.changeRevision = String(++f.revision);
      result = { normalizedValue: v.values[args.parameterId], dirty: v.dirty, changeRevision: v.changeRevision };
      if (f.holdReply?.parameterId === args.parameterId && f.holdReply.gesture === args.gesture) { f.holdReply = null; await new Promise(resolve => { f.releaseReply = resolve; }); f.releaseReply = null; }
    } else if (method === 'saveVoice') { const v = voices.get(args.voiceId); if (v.savedRevision !== args.expectedRevision) throw Error('Incorrect saved revision'); v.dirty = false; v.changeRevision = String(++f.revision); v.savedRevision = String(Number(v.savedRevision)+1); saved.set(v.id, structuredClone(v)); result = v; }
    else if (method === 'revertVoice') { result = structuredClone(saved.get(args.voiceId)); result.changeRevision = String(++f.revision); voices.set(args.voiceId, result); }
    else return { requestId: request.requestId, ok: false, error: { code: 'unsupported', message: method } };
    return { requestId: request.requestId, ok: true, result: structuredClone(result) };
  } };
});
const button = name => page.getByRole('button', { name, exact: true });
const slider = () => page.getByRole('slider', { name: 'Transpose', exact: true });
const idle = async () => { await page.waitForTimeout(120); };
const hold = () => page.evaluate(() => { window.__voiceFixture.hold = true; });
const release = async () => { await page.waitForFunction(() => !!window.__voiceFixture.release); await page.evaluate(() => window.__voiceFixture.release()); };
const trace = () => page.evaluate(() => window.__voiceFixture.requests.filter(r => ['setVoiceParameter', 'saveVoice', 'revertVoice'].includes(r.method)));
try {
  await page.goto(process.env.ATLAS_TEST_URL || 'http://127.0.0.1:5187'); await button('Sound Studio').click();
  await page.getByRole('button', { name: /^Animal A species voice/ }).click(); await page.getByRole('heading', { name: 'Animal A', exact: true }).waitFor();
  assert.equal(await page.locator('.studio__center [role="slider"]').count(), 4);
  assert.equal(await page.getByRole('radio', { name: 'Struck', exact: true }).isDisabled(), true);
  assert.match(await page.locator('.envelope').innerText(), /T60 TO −60 dB/i); assert.match(await page.locator('.studio__center').innerText(), /independent species voice/);
  assert.equal(await button('Audition selected event').isDisabled(), true);
  // Both gestures are queued before the first change acknowledgement.
  await hold(); await slider().press('ArrowRight'); await slider().press('ArrowRight'); await release();
  await page.waitForFunction(() => document.querySelector('[role="slider"][aria-label="Transpose"]')?.getAttribute('aria-valuetext') === '+2 st');
  await button('Undo').click(); await page.waitForFunction(() => document.querySelector('[role="slider"][aria-label="Transpose"]')?.getAttribute('aria-valuetext') === '+1 st');
  await button('Redo').click(); await page.waitForFunction(() => document.querySelector('[role="slider"][aria-label="Transpose"]')?.getAttribute('aria-valuetext') === '+2 st');
  await button('Revert').click(); await page.waitForFunction(() => document.querySelector('[role="slider"][aria-label="Transpose"]')?.getAttribute('aria-valuetext') === '+0 st');
  await hold(); await slider().press('ArrowRight'); await button('Save voice').click(); await button('Saving…').waitFor();
  assert.equal((await trace()).filter(r => r.method === 'saveVoice').length, 0);
  await release(); await page.getByText('Saved rev 2', { exact: true }).waitFor(); assert.equal(await slider().getAttribute('aria-valuetext'), '+1 st');
  let requests = await trace(); assert.equal(requests.at(-1).method, 'saveVoice'); assert.equal(requests.at(-2).args.gesture, 'end');
  await hold(); await slider().press('ArrowRight'); await button('Revert').click(); await button('Reverting…').waitFor();
  assert.equal((await trace()).filter(r => r.method === 'revertVoice').length, 1); await release(); await button('Revert').waitFor(); await idle();
  assert.equal(await slider().getAttribute('aria-valuetext'), '+1 st');
  await slider().press('ArrowRight'); await page.getByText('Unsaved changes', { exact: true }).waitFor(); await idle();
  await hold(); await slider().press('ArrowRight'); await button('Undo').click(); await release();
  await page.waitForFunction(() => document.querySelector('[role="slider"][aria-label="Transpose"]')?.getAttribute('aria-valuetext') === '+2 st');
  await page.getByRole('button', { name: /^Edit Transpose,/ }).click(); await page.getByRole('textbox', { name: 'Transpose value', exact: true }).fill('2.4 st');
  await page.getByRole('textbox', { name: 'Transpose value', exact: true }).press('Enter'); await idle(); assert.equal(await slider().getAttribute('aria-valuetext'), '+2 st');
  await hold(); await slider().press('ArrowRight'); await page.getByRole('button', { name: /^Animal B species voice/ }).click(); await page.getByRole('heading', { name: 'Animal B', exact: true }).waitFor();
  await release(); await idle(); assert.equal(await slider().getAttribute('aria-valuetext'), '+0 st'); assert.equal(await button('Save voice').isDisabled(), true);
  await page.getByRole('button', { name: /^Animal A species voice • unsaved/ }).waitFor();
  await button('Atlas').click(); await button('Sound Studio').click();
  await page.getByRole('button', { name: /^Animal A species voice • unsaved/ }).waitFor();
  await page.getByRole('heading', { name: 'Animal B', exact: true }).waitFor();
  await page.evaluate(() => { window.__voiceFixture.reject = true; }); await slider().press('ArrowRight'); await page.getByText('Fixture rejected parameter', { exact: true }).waitFor(); await idle();
  assert.equal(await slider().getAttribute('aria-valuetext'), '+0 st'); assert.equal(await button('Save voice').isDisabled(), true);
  assert.equal(await button('Undo').isDisabled(), true, 'Rejected edit must not create Undo history');
  // Explicit Revert recovers from a failed, fully drained gesture without a dummy edit.
  await button('Revert').click(); await page.getByText('Fixture rejected parameter', { exact: true }).waitFor({ state: 'hidden' });
  await page.waitForFunction(() => [...document.querySelectorAll('.studio__center button')].find(button => button.textContent === 'Revert')?.disabled);
  assert.equal(await button('Revert').isDisabled(), true); assert.equal(await button('Save voice').isDisabled(), true);
  // A failed edit after a valid edit cannot become an Undo/Redo entry.
  await slider().press('ArrowRight'); await idle();
  await page.evaluate(() => { window.__voiceFixture.reject = true; }); await slider().press('ArrowRight');
  await page.getByText('Fixture rejected parameter', { exact: true }).waitFor(); assert.equal(await button('Undo').isDisabled(), true);
  // Undo is blocked while the failed queue needs recovery; Revert clears both.
  await button('Revert').click(); await page.waitForFunction(() => [...document.querySelectorAll('.studio__center button')].find(button => button.textContent === 'Revert')?.disabled);
  assert.equal(await button('Undo').isDisabled(), true);
  // Older tuning end acknowledgement must not rewind newer gain dirty metadata.
  await slider().press('ArrowRight'); await idle();
  await page.evaluate(() => { window.__voiceFixture.holdReply = { parameterId: 'tuning', gesture: 'end' }; });
  await slider().press('ArrowLeft'); await page.waitForFunction(() => !!window.__voiceFixture.releaseReply);
  await page.getByRole('slider', { name: 'Gain', exact: true }).press('Home'); await page.getByText('Unsaved changes', { exact: true }).waitFor();
  await page.evaluate(() => window.__voiceFixture.releaseReply()); await idle();
  assert.equal(await button('Save voice').isDisabled(), false); await page.getByText('Unsaved changes', { exact: true }).waitFor();
  // Host-local revision restarts at zero. Explicit Revert replaces the old
  // acknowledged snapshot and permits valid new low-revision edits.
  await page.evaluate(() => { window.__voiceFixture.restart(); window.__voiceFixture.reject = true; });
  await slider().press('ArrowRight'); await page.getByText('Fixture rejected parameter', { exact: true }).waitFor();
  assert.equal(await slider().getAttribute('aria-disabled'), 'true'); assert.equal(await button('Save voice').isDisabled(), true);
  await button('Revert').click(); await page.getByText('Reload saved voice to continue', { exact: true }).waitFor({ state: 'hidden' });
  assert.equal(await slider().getAttribute('aria-valuetext'), '+0 st');
  await slider().press('ArrowRight'); await page.getByText('Unsaved changes', { exact: true }).waitFor(); await idle();
  assert.equal(await slider().getAttribute('aria-valuetext'), '+1 st'); assert.equal(await button('Save voice').isDisabled(), false);
  // A delayed full getVoice read cannot replace locally acknowledged edits.
  await page.evaluate(() => { const f = window.__voiceFixture; f.holdRead = true; f.emit({ type: 'voiceParameterChanged', voiceId: 'species:b', parameterId: 'tuning', normalizedValue: .5, changeRevision: '99999', origin: 'restore' }); });
  await page.waitForFunction(() => !!window.__voiceFixture.releaseRead); await slider().press('ArrowRight'); await idle();
  await page.evaluate(() => window.__voiceFixture.releaseRead()); await idle(); assert.equal(await slider().getAttribute('aria-valuetext'), '+2 st');
  // Preparation can register species absent from the latest catalogue while
  // Studio is mounted. Completing preparation must refresh its voice library.
  await button('Atlas').click(); await page.getByRole('radio', { name: 'Replay', exact: true }).click();
  await page.getByRole('button', { name: /New species piece/ }).click(); await page.waitForFunction(() => !!window.__voiceFixture.releasePrepare);
  await button('Sound Studio').click(); assert.equal(await page.getByRole('button', { name: /^Animal C species voice/ }).count(), 0);
  await page.evaluate(() => window.__voiceFixture.releasePrepare()); await page.getByRole('button', { name: /^Animal C species voice/ }).waitFor();
  assert.deepEqual(errors, []);
  console.log(JSON.stringify({ checks: ['metadata/control gating', 'AD/T60 graph and independent ownership', 'Save/Revert/Undo drain deferred gestures', 'canonical quantization updates display', 'late A reply cannot mutate B', 'rejected edit restores acknowledged value', 'Revert recovers failed gesture without dummy edit', 'rapid gestures Undo and Redo use accepted starts', 'failed edits do not enter history', 'late old-voice dirty badge updates', 'older parameter reply cannot rewind global dirty metadata', 'Revert recovers a reset host-local revision', 'delayed full snapshot cannot overwrite local edits', 'dirty badges survive Atlas navigation', 'prepared material refreshes newly registered species'], pageErrors: errors }));
} finally { await browser.close(); }
