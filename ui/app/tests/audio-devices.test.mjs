import test from 'node:test';
import assert from 'node:assert/strict';
import { HostClient } from '../src/host/client.ts';
import { validAudioDeviceCatalog } from '../src/host/audioDevices.ts';

globalThis.window = { setTimeout, clearTimeout };
const device = { id: 'output-a', name: 'Silent output', type: 'Test', isDefault: true, outputChannels: 2, sampleRates: [44100, 48000], bufferFrames: [128, 256], defaultBufferFrames: 256 };
const catalog = { available: true, devices: [device], configuration: { mode: 'systemDefault', deviceId: null, bufferFrames: 0 }, active: null, controlRevision: '1', preparationId: null, generation: '0', error: null };

test('device catalogs preserve unavailable metadata and bounded silent configurations', () => {
  assert.equal(validAudioDeviceCatalog(catalog), true);
  assert.equal(validAudioDeviceCatalog({ ...catalog, devices: [{ ...device, outputChannels: 128 }] }), true);
  assert.equal(validAudioDeviceCatalog({ ...catalog, devices: [] }), true);
  assert.equal(validAudioDeviceCatalog({ ...catalog, configuration: { mode: 'explicitDevice', deviceId: 'disconnected', bufferFrames: 128 }, devices: [{ ...device, outputChannels: 0, sampleRates: [], bufferFrames: [], defaultBufferFrames: 0 }] }), true);
  assert.equal(validAudioDeviceCatalog({ ...catalog, active: { deviceId: device.id, name: device.name, type: device.type, sampleRate: 48000, bufferFrames: 256, outputChannels: 2 } }), true);
});

test('malformed native catalogs fail closed before becoming device choices', () => {
  const invalid = [null, {}, { ...catalog, generation: '-1' }, { ...catalog, controlRevision: '9223372036854775808' },
    { ...catalog, configuration: { ...catalog.configuration, deviceId: 'unexpected' } },
    { ...catalog, configuration: { mode: 'explicitDevice', deviceId: null, bufferFrames: 0 } },
    { ...catalog, configuration: { ...catalog.configuration, bufferFrames: 1.5 } },
    { ...catalog, devices: [device, device] },
    { ...catalog, devices: Array.from({ length: 129 }, (_, i) => ({ ...device, id: String(i) })) },
    ...[{ sampleRates: [NaN] }, { bufferFrames: [0] }, { bufferFrames: [128, 128] }, { sampleRates: Array.from({ length: 65 }, (_, i) => 44000 + i) }, { outputChannels: -1 }, { outputChannels: 65537 }, { defaultBufferFrames: Infinity }, { name: '' }].map(change => ({ ...catalog, devices: [{ ...device, ...change }] })),
    { ...catalog, active: { deviceId: device.id, name: device.name, type: device.type, sampleRate: 48000, bufferFrames: 256, outputChannels: 0 } }];
  for (const value of invalid) assert.equal(validAudioDeviceCatalog(value), false);
});

test('device mutations use advertised methods, validate replies and invalidate earlier playback reads', async () => {
  const calls = []; let payload = catalog;
  const caps = { protocolVersion: 1, implementation: 'native', supportedMethods: ['getCapabilities', 'getAudioDevices', 'setAudioDevice'], audioAvailable: true, maxEventBatch: 500, maxVisibleMarkers: 500 };
  const client = new HostClient({ request: async request => { calls.push(request); return { requestId: request.requestId, ok: true, result: request.method === 'getCapabilities' ? caps : payload }; } });
  await client.connect();
  assert.equal((await client.call('getAudioDevices', {})).ok, true);
  const revision = client.playbackRevision;
  const args = { controlRevision: catalog.controlRevision, preparationId: null, generation: '0', configuration: { mode: 'explicitDevice', deviceId: 'output-a', bufferFrames: 128 } };
  assert.equal((await client.call('setAudioDevice', args)).ok, true);
  assert.equal(client.playbackRevision, revision + 1);
  assert.deepEqual(calls.at(-1).args, args);
  payload = { ...catalog, devices: [device, device] };
  assert.equal((await client.call('getAudioDevices', {})).error.code, 'internal');
});
