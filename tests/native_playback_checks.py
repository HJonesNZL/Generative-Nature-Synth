"""Actual native playback protocol with a test-only fake output; no owner DB or hardware."""
import json
from pathlib import Path
import selectors
import subprocess
import sys
import tempfile
import time
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from animal_atlas.model import event
from animal_atlas.store import Store
from animal_atlas.sessions import Sessions


class NativePlaybackTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / 'synthetic.sqlite3'
        self.store = Store(self.path)
        self.store.ingest('playback', [(event('playback', str(i), '2026-01-01T00:00:00.%06dZ' % offset,
            'acoustic_species_detection', 'station_detection', -41, 174,
            station_id='playback:station', species_id='playback:species', provenance_mode='demo'), {})
            for i, offset in enumerate((0, 100000, 200000))], True)
        self.sessions = Sessions(self.store)
        self.saved = self.sessions.snapshot('Synthetic playback', observed_from='2026-01-01T00:00:00Z',
                                            observed_to='2026-01-01T00:00:02Z')
        self.other = self.sessions.snapshot('Synthetic shorter window', observed_from='2026-01-01T00:00:00Z',
                                            observed_to='2026-01-01T00:00:00.200000Z')
        self.empty = self.sessions.snapshot('Synthetic empty window', observed_from='2026-01-02T00:00:00Z',
                                            observed_to='2026-01-02T00:00:00.200000Z')
        self.process = subprocess.Popen([str(BINARY), 'ui-host', '--db', str(self.path)], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.counter = 0

    def tearDown(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)
            self.fail('test playback host did not quiesce on EOF')
        finally:
            self.selector.close()
            self.process.stdout.close()
            self.process.stderr.close()
            self.temp.cleanup()
        self.assertEqual(self.process.returncode, 0)

    def raw(self, text):
        self.process.stdin.write(text + '\n')
        self.process.stdin.flush()
        self.assertTrue(self.selector.select(timeout=5), 'playback host reply timed out')
        line = self.process.stdout.readline()
        self.assertTrue(line, 'playback host exited before replying')
        return json.loads(line)

    def request(self, method, args=None, error=None):
        self.counter += 1
        identity = 'playback-test-' + str(self.counter)
        reply = self.raw(json.dumps(dict(protocolVersion=1, requestId=identity, method=method, args=args or {})))
        self.assertEqual(reply['requestId'], identity)
        self.assertEqual(reply['ok'], error is None, reply)
        if error is not None:
            self.assertEqual(reply['error']['code'], error, reply)
            return reply['error']
        return reply['result']

    def prepare(self, saved=None, speed='1'):
        return self.request('prepareReplay', dict(sessionId=(saved or self.saved)['id'], basis='observed',
            speedNumerator=speed, speedDenominator='1'))

    def transport(self, action, identity=None, **extra):
        value = identity or self.request('getTransport')
        return self.request('setTransport', dict(action=action, preparationId=value['preparationId'],
            generation=value['generation'], **extra))

    def start(self, saved=None):
        prepared = self.prepare(saved)
        audio = self.request('setAudioEnabled', dict(enabled=True))
        self.assertEqual(audio['status'], 'running')
        played = self.transport('play')
        self.assertEqual(played['state'], 'playing')
        return prepared, played

    def wait_state(self, state):
        end = time.monotonic() + 3
        while True:
            current = self.request('getTransport')
            if current['state'] == state:
                return current
            self.assertLess(time.monotonic(), end, 'callback never reached ' + state)
            time.sleep(.01)

    def test_capabilities_distinguish_audio_from_unsupported_instrument_features(self):
        caps = self.request('getCapabilities')
        self.assertEqual(caps['implementation'], 'native')
        self.assertTrue(caps['audioAvailable'])
        for method in ('setTransport', 'getTransport', 'setAudioEnabled', 'getAudioState'):
            self.assertIn(method, caps['supportedMethods'])
        for method in ('setVoiceParameter', 'startRender', 'setMasterGain', 'setLoop', 'queryHistory', 'setCollection'):
            self.assertNotIn(method, caps['supportedMethods'])
            self.request(method, error='unsupported')
        audio = self.request('getAudioState')
        self.assertEqual(audio['status'], 'disabled')
        self.assertEqual(self.request('setTransport', dict(action='panic'))['state'], 'stopped')
        self.assertEqual(self.request('setTransport', dict(action='stop'))['state'], 'stopped')
        self.assertEqual(audio['masterGainDb'], -18)
        self.assertIsNone(audio['peakDbfs'])
        self.assertIsNone(audio['activeSynthVoices'])
        self.assertFalse(audio['meterIsSimulated'])

    def test_prepare_and_enable_never_autoplay_and_audio_duration_includes_tail(self):
        self.request('setAudioEnabled', dict(enabled=True), error='unavailable')
        before = self.request('getTransport')
        self.assertIsNone(before['preparationId'])
        prepared = self.prepare(self.other)
        self.assertGreater(int(prepared['durationUs']), 200000)
        self.assertEqual(self.request('getAudioState')['status'], 'disabled')
        identity = self.request('getTransport')
        self.request('setTransport', dict(action='play', preparationId=identity['preparationId'],
            generation=identity['generation']), error='unavailable')
        self.request('setAudioEnabled', dict(enabled=True))
        time.sleep(.03)
        current = self.request('getTransport')
        self.assertEqual(current['state'], 'stopped')
        self.assertEqual(current['positionUs'], '0')
        self.assertEqual(current['durationUs'], prepared['durationUs'])

    def test_callback_position_pause_seek_stop_and_panic(self):
        _, played = self.start()
        time.sleep(.04)
        current = self.request('getTransport')
        self.assertGreater(int(current['positionUs']), int(played['positionUs']))
        self.transport('pause')
        paused = self.wait_state('paused')
        time.sleep(.03)
        self.assertEqual(self.request('getTransport')['positionUs'], paused['positionUs'])
        sought = self.transport('seek', positionUs='500000')
        self.assertEqual(sought['state'], 'paused')
        self.assertEqual(sought['positionUs'], '500000')
        self.assertGreater(int(sought['generation']), int(paused['generation']))
        self.transport('play')
        stopped = self.request('setTransport', dict(action='stop'))
        self.assertGreater(int(stopped['generation']), int(sought['generation']))
        self.assertEqual(self.wait_state('stopped')['positionUs'], '0')
        self.transport('play')
        panic = self.request('setTransport', dict(action='panic'))
        self.assertEqual(panic['state'], 'stopped')
        self.assertEqual(panic['positionUs'], '0')

    def test_material_browsing_cannot_replace_active_playback(self):
        self.start()
        before = self.request('getTransport')
        other_material = dict(kind='session', sessionId=self.empty['id'])
        self.assertEqual(self.request('getMaterialEvents', dict(material=other_material, offset=0, limit=10))['total'], 0)
        self.request('getCatalog', dict(material=other_material))
        after = self.request('getTransport')
        self.assertEqual(after['preparationId'], before['preparationId'])
        self.assertEqual(after['generation'], before['generation'])
        self.assertEqual(after['durationUs'], before['durationUs'])
        self.assertEqual(after['state'], 'playing')
        self.assertGreaterEqual(int(after['positionUs']), int(before['positionUs']))

    def test_new_preparation_disables_and_invalidates_previous_commands(self):
        self.start()
        previous = self.request('getTransport')
        replacement = self.prepare(self.empty)
        current = self.request('getTransport')
        self.assertNotEqual(replacement['preparationId'], previous['preparationId'])
        self.assertEqual(current['positionUs'], '0')
        self.assertEqual(current['state'], 'stopped')
        self.assertEqual(self.request('getAudioState')['status'], 'disabled')
        self.request('setTransport', dict(action='play', preparationId=previous['preparationId'],
            generation=previous['generation']), error='stale_generation')
        self.request('setAudioEnabled', dict(enabled=True))
        self.transport('seek', positionUs='100000')
        self.request('setTransport', dict(action='play', preparationId=current['preparationId'],
            generation=current['generation']), error='stale_generation')

    def test_completion_tracks_callback_including_empty_requested_window(self):
        prepared, _ = self.start(self.empty)
        finished = self.wait_state('stopped')
        self.assertEqual(finished['positionUs'], prepared['durationUs'])
        self.assertEqual(finished['durationUs'], '200000')
        self.assertEqual(self.request('getAudioState')['status'], 'running')

    def test_corrupt_preparation_stops_old_audio_and_never_exposes_new_material(self):
        self.start()
        old = self.request('getTransport')
        with self.store.connect() as db:
            db.execute('UPDATE sessions SET name=? WHERE id=?', ('tampered fixture', self.empty['id']))
        self.request('prepareReplay', dict(sessionId=self.empty['id'], basis='observed',
            speedNumerator='1', speedDenominator='1'), error='unavailable')
        self.assertEqual(self.request('getAudioState')['status'], 'disabled')
        current = self.request('getTransport')
        self.assertEqual(current['preparationId'], old['preparationId'])
        self.assertEqual(current['state'], 'stopped')
        self.assertEqual(current['positionUs'], '0')

    def test_malformed_requests_fail_closed_and_fresh_status_survives(self):
        self.start()
        identity = self.request('getTransport')
        common = dict(preparationId=identity['preparationId'], generation=identity['generation'])
        cases = [('setAudioEnabled', dict(enabled=1)), ('setAudioEnabled', dict(enabled=True, extra=0)),
            ('getAudioState', dict(extra=0)), ('getTransport', dict(extra=0)),
            ('setTransport', dict(action='loop', **common)), ('setTransport', dict(action='play')),
            ('setTransport', dict(action='pause', preparationId=common['preparationId'])),
            ('setTransport', dict(action='seek', positionUs='-1', **common)),
            ('setTransport', dict(action='seek', positionUs=0, **common)),
            ('setTransport', dict(action='seek', positionUs='9999999999999999999999999', **common)),
            ('setTransport', dict(action='seek', positionUs='3000000', **common)),
            ('setTransport', dict(action='play', positionUs='0', **common)),
            ('setTransport', dict(action='play', generation=True, preparationId=common['preparationId']))]
        for method, args in cases:
            with self.subTest(method=method, args=args):
                self.request(method, args, error='invalid_request')
        duplicate = '{"protocolVersion":1,"requestId":"duplicate","method":"setTransport","args":{"action":"panic","action":"play"}}'
        for raw in (duplicate, '[]', 'not-json', 'x' * 16385):
            with self.subTest(raw=raw[:80]):
                self.assertFalse(self.raw(raw)['ok'])
        status = self.request('getTransport')
        self.assertEqual(status['preparationId'], identity['preparationId'])
        self.assertEqual(status['generation'], identity['generation'])
        self.assertGreaterEqual(int(status['positionUs']), int(identity['positionUs']))

    def test_playback_never_writes_disposable_library(self):
        before = self.path.read_bytes()
        high_water = self.store.high_water()
        self.start()
        self.transport('pause')
        self.wait_state('paused')
        self.transport('seek', positionUs='0')
        self.transport('play')
        self.request('setTransport', dict(action='panic'))
        self.request('setAudioEnabled', dict(enabled=False))
        self.assertEqual(self.path.read_bytes(), before)
        self.assertEqual(self.store.high_water(), high_water)
        self.assertEqual(len(self.sessions.list()), 3)


if __name__ == '__main__':
    unittest.main()
