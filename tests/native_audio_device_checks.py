"""Output-device protocol on disposable synthetic studio output; no hardware."""
import copy
import time
import unittest
from native_voice_checks import NativeVoiceTests


class AudioDeviceTests(NativeVoiceTests):
    def catalog(self):
        return self.request('getAudioDevices')

    def configure(self, configuration, catalog=None, error=None):
        current = catalog or self.catalog()
        return self.request('setAudioDevice', dict(controlRevision=current['controlRevision'],
            preparationId=current['preparationId'], generation=current['generation'],
            configuration=configuration), error=error)

    def prepare_piece(self):
        return self.request('prepareReplay', dict(sessionId=self.saved['id'], basis='observed',
            speedNumerator='1', speedDenominator='1'))

    def play(self):
        current = self.request('getTransport')
        return self.request('setTransport', dict(action='play', preparationId=current['preparationId'],
            generation=current['generation']))

    def test_discovery_and_preparation_free_selection_remain_silent(self):
        for method in ('getAudioDevices', 'setAudioDevice'):
            self.assertIn(method, self.request('getCapabilities')['supportedMethods'])
        initial = self.catalog()
        self.assertTrue(initial['available'])
        self.assertIsNone(initial['active'])
        self.assertIsNone(initial['preparationId'])
        self.assertEqual(initial['generation'], '0')
        self.assertEqual(initial['configuration'], dict(mode='systemDefault', deviceId=None, bufferFrames=0))
        self.assertEqual({entry['id'] for entry in initial['devices']}, {'fixture-default', 'fixture-external'})
        configured = self.configure(dict(mode='explicitDevice', deviceId='fixture-external', bufferFrames=256), initial)
        self.assertIsNone(configured['active'])
        self.assertGreater(int(configured['controlRevision']), int(initial['controlRevision']))
        self.assertFalse(self.starts.exists(), 'selection/discovery opened output')
        self.prepare_piece()
        self.assertEqual(self.request('getAudioState')['status'], 'disabled')
        audio = self.request('setAudioEnabled', dict(enabled=True))
        self.assertEqual((audio['deviceName'], audio['sampleRate'], audio['bufferFrames']), ('Synthetic external output', 48000, 256))
        active = self.catalog()['active']
        self.assertEqual((active['deviceId'], active['sampleRate'], active['bufferFrames'], active['outputChannels']), ('fixture-external', 48000, 256, 2))
        time.sleep(.03)
        self.assertEqual(self.client.raw('fixture:peak')['peak'], 0)
        state = self.request('getTransport')
        self.assertEqual((state['state'], state['positionUs']), ('stopped', '0'))
        audio = self.request('getAudioState')
        self.assertGreater(int(audio['callbackCount']), 0)
        self.assertGreater(int(audio['callbackFrames']), 0)
        self.assertEqual(audio['faultCount'], '0')

    def test_rejected_and_stale_changes_preserve_running_device(self):
        self.prepare_piece()
        self.request('setAudioEnabled', dict(enabled=True))
        self.play()
        current = self.catalog()
        starts = self.starts.read_text()
        for configuration, error in (
            (dict(mode='explicitDevice', deviceId='../../device', bufferFrames=0), 'invalid_request'),
            (dict(mode='explicitDevice', deviceId='fixture-external', bufferFrames=64), 'unavailable'),
            (dict(mode='explicitDevice', deviceId='not-enumerated', bufferFrames=0), 'unavailable'),
            (dict(mode='systemDefault', deviceId='fixture-default', bufferFrames=0), 'invalid_request'),
        ):
            self.configure(configuration, current, error)
            self.assertEqual(self.request('getAudioState')['status'], 'running')
        stale = copy.deepcopy(current)
        stale['generation'] = str(int(stale['generation']) - 1)
        self.configure(dict(mode='systemDefault', deviceId=None, bufferFrames=128), stale, 'stale_generation')
        self.assertEqual(self.starts.read_text(), starts)
        changed = self.configure(dict(mode='systemDefault', deviceId=None, bufferFrames=128), current)
        self.assertIsNone(changed['active'])
        self.assertGreater(int(changed['generation']), int(current['generation']))
        self.assertEqual(self.request('getAudioState')['status'], 'disabled')
        self.configure(dict(mode='systemDefault', deviceId=None, bufferFrames=128), current, 'stale_generation')
        self.request('setAudioEnabled', dict(enabled=True))
        self.client.raw('fixture:reset-peak')
        time.sleep(.03)
        self.assertEqual(self.client.raw('fixture:peak')['peak'], 0)
        self.assertEqual(self.request('getTransport')['state'], 'stopped')

    def test_device_loss_preserves_selection_without_fallback_or_restart(self):
        self.configure(dict(mode='explicitDevice', deviceId='fixture-external', bufferFrames=128))
        self.prepare_piece()
        self.request('setAudioEnabled', dict(enabled=True))
        self.play()
        previous = self.catalog()
        starts = self.starts.read_text()
        self.assertEqual(self.client.raw('fixture:remove-device')['fixture'], 'device-removed')
        lost = self.catalog()
        self.assertEqual(lost['configuration']['deviceId'], 'fixture-external')
        self.assertIsNone(lost['active'])
        self.assertTrue(lost['error'])
        self.assertGreater(int(lost['controlRevision']), int(previous['controlRevision']))
        self.assertEqual(self.request('getAudioState')['status'], 'device_lost')
        self.request('setAudioEnabled', dict(enabled=True), error='unavailable')
        self.assertEqual(self.starts.read_text(), starts)
        self.client.raw('fixture:restore-device')
        restored = self.catalog()
        self.assertIsNone(restored['active'])
        self.assertEqual(self.starts.read_text(), starts)
        self.configure(restored['configuration'], lost, 'stale_generation')
        self.configure(restored['configuration'], restored)
        self.assertEqual(self.request('getAudioState')['status'], 'disabled')
        self.request('setAudioEnabled', dict(enabled=True))
        self.client.raw('fixture:reset-peak')
        time.sleep(.03)
        self.assertEqual(self.client.raw('fixture:peak')['peak'], 0)
        self.assertEqual(self.request('getTransport')['state'], 'stopped')
        self.assertGreaterEqual(int(self.request('getAudioState')['faultCount']), 1)

    def test_device_envelope_and_configuration_are_strictly_bounded(self):
        initial = self.catalog()
        configuration = dict(mode='systemDefault', deviceId=None, bufferFrames=0)
        for bad in (dict(configuration, mode='auto'), dict(configuration, bufferFrames=-1),
                    dict(configuration, bufferFrames=4097), dict(configuration, bufferFrames=True),
                    dict(configuration, extra=True), dict(mode='explicitDevice', deviceId='x'*65, bufferFrames=0)):
            self.configure(bad, initial, 'invalid_request')
        self.request('getAudioDevices', dict(extra=0), error='invalid_request')
        self.assertFalse(self.starts.exists())


for name in NativeVoiceTests.__dict__:
    if name.startswith('test_'):
        setattr(AudioDeviceTests, name, None)
del NativeVoiceTests

if __name__ == '__main__':
    unittest.main()
