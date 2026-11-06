// Real component probes with synthetic injected host; never uses native data/audio.
import assert from 'node:assert/strict';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const origin = process.env.ATLAS_TEST_URL || 'http://127.0.0.1:5173';
const browser = await chromium.launch({
    headless: true,
    channel: 'chrome',
    args: ['--enable-unsafe-swiftshader']
});
const errors = [], bridgeCalls = [], checks = [];
const only = process.env.ATLAS_READINESS_CASE;
async function open(scenario) {
    const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
    page.setDefaultTimeout(5000);
    page.on('pageerror', error => errors.push(error.message));
    page.on('request', request => {
        if (request.url().includes('/__atlas/')) {
            bridgeCalls.push(request.url());
        }
    });
    await page.addInitScript(({ scenario }) => {
        const context = HTMLCanvasElement.prototype.getContext;
        HTMLCanvasElement.prototype.getContext = function (type, ...args) {
            return type.startsWith('webgl') ? null : context.call(this, type, ...args);
        };
        const state = window.readiness = {
            calls: [],
            pending: {},
            deferLoads: [],
            deferMutation: null,
            failures: 0,
            failOffset: scenario === 'later-material-error' ? 500 : 0,
            renderState: 'queued',
            renderStates: {}
        };
        const voice = id => ({
            id,
            name: `Voice ${id}`,
            scope: 'species',
            targetId: id,
            inheritsFromId: null,
            type: 'pluck',
            values: { tone: .5 },
            overriddenParameterIds: [],
            savedRevision: '1',
            changeRevision: '0',
            dirty: true
        });
        const event = i => ({
            id: `fixture-${i}`,
            revisionSequence: String(i + 1),
            sourceId: 'synthetic',
            speciesId: i ? 'B' : 'A',
            commonName: null,
            individualId: i ? null : 'tag-A',
            deploymentId: i ? null : 'deployment-A',
            stationId: 'receiver',
            eventKind: i ? 'acoustic_species_detection' : 'gps_fix',
            locationKind: i ? 'station_detection' : 'individual_fix',
            position: {
                latitude: i ? -41 : 40,
                longitude: i ? 174 : -70,
                uncertaintyMetres: i ? null : 20
            },
            observedAtUtc: '2020-01-01T00:00:00.000001Z',
            firstSeenAtUtc: '2020-01-01T00:00:01.000001Z',
            confidence: null,
            attribution: 'Synthetic readiness fixture',
            qualityFlags: [],
            provenance: 'demo'
        });
        const session = {
            id: 'fixture-session',
            name: 'Readiness fixture',
            state: 'frozen',
            origin: 'archive_snapshot',
            eventCount: scenario === 'later-material-error' ? 501 : 2,
            captureTruncated: false,
            metadataIntegrity: 'sha256-envelope-v1-verified',
            observedBoundsUs: ['1577836800000000', '1577836801000000'],
            arrivalBoundsUs: ['1577836801000000', '1577836802000000']
        };
        const methods = ['getCapabilities', 'getChanges', 'getCatalog', 'listSessions', 'prepareReplay', 'getMaterialEvents'];
        if (scenario.startsWith('voice')) {
            methods.push('getVoiceLibrary', 'getVoice', 'getMappings', 'setVoiceParameter', 'setVoiceType', 'saveVoice', 'revertVoice');
        }
        if (scenario.startsWith('render')) {
            methods.push('listSoundscapes', 'startSoundscapeRender', 'getRenderJob', ...(scenario !== 'render-poll' ? ['cancelRender'] : []));
        }
        const deferred = key => new Promise(resolve => {
            (state.pending[key] ??= []).push(resolve);
        });
        state.release = key => {
            for (const resolve of state.pending[key] ?? []) {
                resolve();
            }
            delete state.pending[key];
        };
        state.listeners = new Set();
        state.emit = notification => {
            for (const listener of state.listeners) {
                listener(notification);
            }
        };
        window.atlasHost = { subscribe: listener => {
                state.listeners.add(listener);
                return () => state.listeners.delete(listener);
            }, request: async (request) => {
                state.calls.push({ method: request.method, args: request.args });
                const { method, args } = request;
                let result;
                if (method === 'getCapabilities') {
                    result = {
                        protocolVersion: 1,
                        implementation: 'mock',
                        supportedMethods: methods,
                        audioAvailable: false,
                        maxEventBatch: 500,
                        maxVisibleMarkers: 500
                    };
                }
                else if (method === 'getChanges') {
                    result = {
                        storeId: 'readiness-fixture',
                        nextSequence: '2',
                        events: args.afterSequence === '0' ? [0, 1].map(i => ({
                            event: event(i),
                            liveEligible: false,
                            change: 'baseline'
                        })) : []
                    };
                }
                else if (method === 'getCatalog') {
                    result = {
                        species: ['A', 'B'].map(id => ({
                            id,
                            commonName: `Bird ${id}`,
                            scientificName: '',
                            group: 'unclassified'
                        })),
                        stations: [{
                                id: 'receiver',
                                name: 'Synthetic receiver',
                                sourceId: 'synthetic',
                                latitude: -41,
                                longitude: 174,
                                positionDescription: 'Reported receiver'
                            }],
                        sources: [],
                        limited: false
                    };
                }
                else if (method === 'listSessions') {
                    result = [session];
                }
                else if (method === 'prepareReplay') {
                    result = {
                        preparationId: 'fixture-preparation',
                        generation: '1',
                        durationUs: '1000000',
                        eventCount: session.eventCount,
                        peakEventsPerSecond: 2,
                        session
                    };
                }
                else if (method === 'getMaterialEvents') {
                    if (scenario.includes('material-error') && args.offset === state.failOffset && state.failures++ === 0) {
                        return {
                            requestId: request.requestId,
                            ok: false,
                            error: { code: 'unavailable', message: 'Injected material read failure' }
                        };
                    }
                    result = { total: session.eventCount, events: Array.from({ length: Math.min(args.limit, session.eventCount - args.offset) }, (_, i) => event(i + args.offset)) };
                }
                else if (method === 'getVoiceLibrary') {
                    result = { entries: [{
                                groupId: 'fixture',
                                groupName: 'Synthetic voices',
                                groupVoiceId: null,
                                species: ['A', 'B'].map(id => ({
                                    speciesId: id,
                                    commonName: `Bird ${id}`,
                                    scientificName: '',
                                    voiceId: id,
                                    individuals: []
                                }))
                            }], parameters: [{
                                id: 'tone',
                                label: 'Tone',
                                unit: 'ratio',
                                min: 0,
                                max: 1,
                                defaultValue: .5,
                                curve: 'linear',
                                proposal: true
                            }] };
                }
                else if (method === 'getVoice') {
                    if (state.deferLoads.includes(args.voiceId)) {
                        await deferred(`load-${args.voiceId}`);
                    }
                    result = voice(args.voiceId);
                }
                else if (['saveVoice', 'revertVoice', 'setVoiceType'].includes(method)) {
                    if (state.deferMutation === method && args.voiceId === 'A') {
                        await deferred(method);
                    }
                    result = {
                        ...voice(args.voiceId),
                        type: args.type ?? 'pluck',
                        dirty: false
                    };
                }
                else if (method === 'getMappings') {
                    result = [];
                }
                else if (method === 'setVoiceParameter') {
                    if (state.deferGesture && args.voiceId === 'A' && args.gesture === 'begin') {
                        await deferred('begin-A');
                    }
                    result = { normalizedValue: args.normalizedValue, changeRevision: '2', dirty: true };
                }
                else if (method === 'listSoundscapes') {
                    result = { items: [{ id: 'a'.repeat(32), name: 'Saved readiness scene', valid: true, contentVerified: true, error: null, contentHash: 'c'.repeat(64), sourceSessionId: session.id, eventCount: 2, durationUs: '1000000', basis: 'observed', speedNumerator: '60', speedDenominator: '1' }], limited: false };
                }
                else if (method === 'startSoundscapeRender') {
                    await deferred('startSoundscapeRender');
                    const jobId = String(state.calls.filter(call => call.method === 'startSoundscapeRender').length).padStart(32, '0');
                    state.renderStates[jobId] = 'queued';
                    result = {
                        jobId,
                        state: 'queued',
                        progress: 0,
                        message: 'Synthetic queued job', soundscapeId: 'a'.repeat(32), artifactId: null, filename: null
                    };
                }
                else if (method === 'getRenderJob') {
                    const snapshot = scenario === 'render-cross-job' ? state.renderStates[args.jobId] : state.renderState;
                    if (state.deferPoll) {
                        await deferred('getRenderJob');
                    }
                    result = {
                        jobId: args.jobId,
                        state: snapshot,
                        progress: snapshot === 'complete' ? 1 : 0,
                        message: 'Synthetic job state'
                    };
                }
                else if (method === 'cancelRender') {
                    if (state.deferCancel) {
                        await deferred('cancelRender');
                    }
                    if (!state.cancelFailed && scenario !== 'render-cross-job') {
                        state.cancelFailed = true;
                        return {
                            requestId: request.requestId,
                            ok: false,
                            error: { code: 'unavailable', message: 'Injected cancel failure' }
                        };
                    }
                    state.renderStates[args.jobId] = 'cancelled';
                    if (scenario !== 'render-cross-job') {
                        state.renderState = 'cancelled';
                    }
                    result = {
                        jobId: args.jobId,
                        state: 'cancelled',
                        progress: 0,
                        message: 'Synthetic cancellation'
                    };
                }
                else {
                    return {
                        requestId: request.requestId,
                        ok: false,
                        error: { code: 'unsupported', message: 'Not offered by synthetic test host' }
                    };
                }
                return {
                    requestId: request.requestId,
                    ok: true,
                    result
                };
            } };
    }, { scenario });
    await page.goto(origin);
    await page.getByRole('button', { name: 'All available species', exact: true }).waitFor();
    return page;
}
async function replay(page) {
    await page.getByRole('radio', { name: 'Replay', exact: true }).click();
    await page.getByRole('button', { name: /Readiness fixture/ }).click();
}
async function studio(page) {
    await page.getByRole('button', { name: 'Sound Studio', exact: true }).click();
    if (await page.getByRole('combobox', { name: 'Saved soundscape', exact: true }).count())
        await page.getByRole('combobox', { name: 'Saved soundscape', exact: true }).selectOption('a'.repeat(32));
}
async function selectVoice(page, id) {
    await page.getByRole('button', { name: `Bird ${id} species voice`, exact: false }).click();
}
async function title(page, id) {
    await page.getByRole('heading', { name: `Voice ${id}`, exact: true }).waitFor();
}
try {
    if (!only || only === 'semantics') {
        const page = await open('semantics');
        await page.locator('.map-locations summary').click();
        await page.locator('.map-locations button').filter({ hasText: 'GPS fix' }).click();
        assert.match(await page.locator('.inspector').innerText(), /GPS position fix[\s\S]*Estimated position/);
        assert.doesNotMatch(await page.locator('.inspector').innerText(), /receiver position|station position/);
        await page.locator('.map-locations button').filter({ hasText: 'receiver,' }).click();
        assert.match(await page.locator('.inspector').innerText(), /receiver position \(not the animal/);
        checks.push('GPS with station metadata retains measured-fix semantics; acoustic receiver retains receiver semantics');
        await page.close();
    }
    for (const scenario of ['material-error', 'later-material-error'].filter(value => !only || only === value)) {
        const page = await open(scenario);
        await replay(page);
        await page.getByText('Material could not be loaded', { exact: true }).waitFor();
        await page.getByRole('button', { name: 'Retry material', exact: true }).click();
        await page.waitForFunction(() => document.querySelector('.map-overview')?.textContent.includes(`${readiness.failOffset ? 501 : 2} records`));
        await page.getByRole('button', { name: 'Retry material', exact: true }).waitFor({ state: 'detached' });
        const offsets = await page.evaluate(() => readiness.calls.filter(call => call.method === 'getMaterialEvents').map(call => call.args.offset));
        assert.deepEqual(offsets, scenario === 'material-error' ? [0, 0] : [0, 500, 0, 500]);
        await studio(page);
        await page.getByRole('button', { name: 'Atlas', exact: true }).click();
        assert.match(await page.locator('.map-overview').innerText(), scenario === 'material-error' ? /2 records/ : /501 records/);
        checks.push(`${scenario}: explicit retry restarts pages without duplicate records and survives screen navigation`);
        await page.close();
    }
    if (!only || only === 'voice') {
        const page = await open('voice');
        await studio(page);
        await page.evaluate(() => readiness.deferLoads.push('A'));
        await selectVoice(page, 'A');
        await page.waitForFunction(() => readiness.pending['load-A']?.length);
        await selectVoice(page, 'B');
        await title(page, 'B');
        await page.evaluate(() => readiness.release('load-A'));
        await page.waitForTimeout(100);
        await title(page, 'B');
        const tone = page.getByRole('slider', { name: 'Tone', exact: true });
        await tone.focus();
        await page.keyboard.press('ArrowUp');
        await page.waitForFunction(() => readiness.calls.some(call => call.method === 'setVoiceParameter'));
        assert.ok((await page.evaluate(() => readiness.calls.filter(call => call.method === 'setVoiceParameter'))).every(call => call.args.voiceId === 'B'));
        await page.evaluate(() => {
            readiness.deferLoads = [];
        });
        for (const method of ['saveVoice', 'revertVoice', 'setVoiceType']) {
            await selectVoice(page, 'A');
            await title(page, 'A');
            await page.evaluate(method => {
                readiness.deferMutation = method;
            }, method);
            if (method === 'setVoiceType') {
                await page.getByRole('radio', { name: 'Air (proposed)', exact: true }).click();
            }
            else {
                await page.getByRole('button', { name: method === 'saveVoice' ? 'Save voice' : 'Revert', exact: true }).click();
            }
            await page.waitForFunction(method => readiness.pending[method]?.length, method);
            await selectVoice(page, 'B');
            await title(page, 'B');
            await page.evaluate(method => readiness.release(method), method);
            await page.waitForTimeout(100);
            await title(page, 'B');
        }
        checks.push('Deferred A load/save/revert/type cannot replace B; parameter gestures target B');
        await page.close();
    }
    if (!only || only === 'voice-gesture') {
        const page = await open('voice-gesture');
        await studio(page);
        await selectVoice(page, 'A');
        await title(page, 'A');
        await page.evaluate(() => {
            readiness.deferGesture = true;
        });
        await page.getByRole('slider', { name: 'Tone', exact: true }).focus();
        await page.keyboard.press('ArrowUp');
        await page.waitForFunction(() => readiness.pending['begin-A']?.length);
        await selectVoice(page, 'B');
        await title(page, 'B');
        await page.evaluate(() => readiness.release('begin-A'));
        await page.waitForFunction(() => readiness.calls.some(call => call.method === 'setVoiceParameter' && call.args.voiceId === 'A' && call.args.gesture === 'end'));
        assert.deepEqual(await page.evaluate(() => readiness.calls.filter(call => call.method === 'setVoiceParameter').map(call => [call.args.voiceId, call.args.gesture])), [['A', 'begin'], ['A', 'change'], ['A', 'end']]);
        await title(page, 'B');
        checks.push('Accepted deferred A gesture receives ordered change/end after selecting B, without targeting B');
        await page.close();
    }
    if (!only || only === 'render-cross-job') {
        const page = await open('render-cross-job');
        await replay(page);
        await studio(page);
        await page.getByRole('button', { name: 'Export saved WAV', exact: true }).click();
        await page.waitForFunction(() => readiness.pending.startSoundscapeRender?.length);
        await page.evaluate(() => readiness.release('startSoundscapeRender'));
        await page.getByText('WAV export queued', { exact: true }).waitFor();
        await page.evaluate(() => {
            readiness.deferCancel = true;
        });
        await page.getByRole('button', { name: 'Cancel export', exact: true }).click();
        await page.waitForFunction(() => readiness.pending.cancelRender?.length);
        await page.evaluate(() => {
            readiness.renderStates['00000000000000000000000000000001'] = 'complete';
            readiness.emit({ type: 'renderProgress', job: {
                    jobId: '00000000000000000000000000000001',
                    state: 'complete',
                    progress: 1,
                    message: 'Synthetic first job completed'
                } });
        });
        await page.getByText('WAV export complete.', { exact: false }).waitFor();
        await page.getByRole('button', { name: 'Export saved WAV', exact: true }).click();
        await page.waitForFunction(() => readiness.pending.startSoundscapeRender?.length);
        await page.evaluate(() => {
            readiness.renderState = 'queued';
            readiness.release('startSoundscapeRender');
        });
        await page.getByText('WAV export queued', { exact: true }).waitFor();
        await page.evaluate(() => readiness.release('cancelRender'));
        await page.waitForFunction(() => readiness.calls.filter(call => call.method === 'getRenderJob' && call.args.jobId === '00000000000000000000000000000002').length >= 2);
        await page.waitForTimeout(100);
        await page.getByText('WAV export queued', { exact: true }).waitFor();
        assert.equal(await page.getByText('WAV export cancelled.', { exact: true }).count(), 0);
        assert.equal(await page.getByRole('button', { name: 'Export saved WAV', exact: true }).isDisabled(), true);
        checks.push('Late first-job cancel acknowledgement cannot replace a newer queued job');
        await page.close();
    }
    for (const scenario of ['render-poll', 'render-cancel'].filter(value => !only || only === value)) {
        const page = await open(scenario);
        await replay(page);
        await studio(page);
        await page.getByRole('button', { name: 'Export saved WAV', exact: true }).click();
        await page.waitForFunction(() => readiness.pending.startSoundscapeRender?.length);
        assert.equal(await page.getByRole('button', { name: /^(Export saved WAV|Starting export…)$/ }).isDisabled(), true);
        await page.evaluate(() => readiness.release('startSoundscapeRender'));
        await page.getByText('WAV export queued', { exact: true }).waitFor();
        assert.equal(await page.getByRole('button', { name: 'Export saved WAV', exact: true }).isDisabled(), true);
        assert.equal(await page.getByRole('button', { name: 'Cancel export', exact: true }).isDisabled(), scenario === 'render-poll');
        assert.equal(await page.evaluate(() => readiness.calls.filter(call => call.method === 'startSoundscapeRender').length), 1);
        if (scenario === 'render-poll') {
            await page.getByRole('button', { name: 'Atlas', exact: true }).click();
            await studio(page);
            await page.getByText('WAV export queued', { exact: true }).waitFor();
            await page.evaluate(() => {
                readiness.renderState = 'complete';
            });
            await page.getByText('WAV export complete.', { exact: false }).waitFor();
            assert.equal(await page.getByRole('button', { name: 'Export saved WAV', exact: true }).isDisabled(), false);
        }
        else {
            await page.evaluate(() => {
                readiness.deferPoll = true;
            });
            await page.waitForFunction(() => readiness.pending.getRenderJob?.length);
            await page.getByRole('button', { name: 'Cancel export', exact: true }).click();
            await page.getByText('Injected cancel failure', { exact: true }).waitFor();
            await page.getByRole('button', { name: 'Cancel export', exact: true }).click();
            await page.getByText('WAV export cancelled.', { exact: true }).waitFor();
            await page.evaluate(() => {
                readiness.release('getRenderJob');
                readiness.emit({ type: 'renderProgress', job: {
                        jobId: '00000000000000000000000000000001',
                        state: 'rendering',
                        progress: .5,
                        message: 'Stale progress'
                    } });
            });
            await page.waitForTimeout(100);
            await page.getByText('WAV export cancelled.', { exact: true }).waitFor();
        }
        checks.push(`${scenario}: pending/queued gating, cancellation capability/error, polling without push and job retention`);
        await page.close();
    }
    assert.deepEqual(errors, []);
    assert.deepEqual(bridgeCalls, [], 'No native fallback in synthetic tests');
    console.log(JSON.stringify({
        checks,
        pageErrors: errors,
        nativeBridgeRequests: bridgeCalls
    }, null, 2));
}
finally {
    await browser.close();
}
