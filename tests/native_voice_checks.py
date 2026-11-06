"""Native studio protocol: isolated SQLite sidecar and clocked fake output, no hardware."""
import json
import os
from pathlib import Path
import selectors
import sqlite3
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


class Client:
    def __init__(self, library, voices, starts):
        environment = dict(os.environ, ATLAS_VOICE_LIBRARY=str(voices), ATLAS_VOICE_OUTPUT_STARTS=str(starts))
        self.process = subprocess.Popen([str(BINARY), 'ui-host', '--db', str(library)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=environment)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.counter = 0

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)
            raise AssertionError('Studio fixture did not quiesce on EOF')
        finally:
            self.selector.close()
            self.process.stdout.close()
        diagnostics = self.process.stderr.read()
        self.process.stderr.close()
        if self.process.returncode != 0:
            raise AssertionError('Studio fixture failed: ' + diagnostics)

    def raw(self, text):
        self.process.stdin.write(text + '\n')
        self.process.stdin.flush()
        if not self.selector.select(timeout=5):
            raise AssertionError('Studio fixture reply timed out')
        line = self.process.stdout.readline()
        if not line:
            raise AssertionError('Studio fixture exited before replying')
        return json.loads(line)

    def request(self, method, args=None, error=None):
        self.counter += 1
        identity = 'studio-test-' + str(self.counter)
        reply = self.raw(json.dumps(dict(protocolVersion=1, requestId=identity, method=method, args=args or {})))
        if reply['requestId'] != identity or reply['ok'] != (error is None):
            raise AssertionError(reply)
        if error is not None:
            if reply['error']['code'] != error:
                raise AssertionError(reply)
            return reply['error']
        return reply['result']


class NativeVoiceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / 'animal-library.sqlite3'
        self.voices = Path(self.temp.name) / 'voice-presets.sqlite3'
        self.starts = Path(self.temp.name) / 'output-starts.txt'
        self.store = Store(self.path)
        records = []
        for index, (offset, species) in enumerate(((0, 'studio:bird'), (100000, 'studio:fish'), (5000000, 'studio:bird'))):
            records.append((event('studio', str(index), '2026-01-01T00:00:%02d.%06dZ' % divmod(offset, 1000000),
                'acoustic_species_detection', 'station_detection', -41, 174, station_id='studio:station',
                species_id=species, provenance_mode='demo'), {}))
        records.append((event('studio', 'long', '2026-01-02T00:00:00Z', 'acoustic_species_detection',
            'station_detection', -41, 174, station_id='studio:station', species_id='studio:other', provenance_mode='demo'), {}))
        self.store.ingest('studio', records, True)
        self.sessions = Sessions(self.store)
        self.saved = self.sessions.snapshot('Synthetic studio', observed_from='2026-01-01T00:00:00Z',
                                           observed_to='2026-01-01T00:00:10Z')
        self.only_bird = self.sessions.snapshot('Single species studio piece', observed_from='2026-01-01T00:00:00Z',
                                               observed_to='2026-01-01T00:00:00.050000Z')
        self.too_long = self.sessions.snapshot('Rejected long studio piece', observed_from='2026-01-02T00:00:00Z',
                                              observed_to='2026-01-02T01:00:00Z')
        self.library_bytes = self.path.read_bytes()
        self.clients = []
        self.client = self.new_client()

    def tearDown(self):
        failures = []
        try:
            for client in self.clients:
                try:
                    client.close()
                except AssertionError as failure:
                    failures.append(str(failure))
            self.assertEqual(self.path.read_bytes(), self.library_bytes, 'Studio controls wrote animal library')
            self.assertFalse(failures, '\n'.join(failures))
        finally:
            self.temp.cleanup()

    def new_client(self):
        client = Client(self.path, self.voices, self.starts)
        self.clients.append(client)
        return client

    def restart(self):
        self.client.close()
        self.clients.remove(self.client)
        self.client = self.new_client()

    def request(self, method, args=None, error=None):
        return self.client.request(method, args, error)

    def voice(self, identity='default', client=None):
        return (client or self.client).request('getVoice', dict(voiceId=identity))

    def phase(self, parameter, value, phase, identity='default', client=None, error=None):
        return (client or self.client).request('setVoiceParameter', dict(voiceId=identity,
            parameterId=parameter, normalizedValue=value, gesture=phase), error)

    def edit(self, parameter, value, identity='default', client=None):
        self.phase(parameter, value, 'begin', identity, client)
        self.phase(parameter, value, 'change', identity, client)
        return self.phase(parameter, value, 'end', identity, client)

    def save(self, identity='default', client=None, error=None):
        active = client or self.client
        revision = self.voice(identity, active)['savedRevision']
        return active.request('saveVoice', dict(voiceId=identity, expectedRevision=revision), error)

    def prepare(self):
        return self.request('prepareReplay', dict(sessionId=self.saved['id'], basis='observed',
            speedNumerator='1', speedDenominator='1'))

    def transport(self, action, **extra):
        current = self.request('getTransport')
        return self.request('setTransport', dict(action=action, preparationId=current['preparationId'],
            generation=current['generation'], **extra))

    def wait_state(self, state):
        deadline = time.monotonic() + 3
        while True:
            current = self.request('getTransport')
            if current['state'] == state:
                return current
            self.assertLess(time.monotonic(), deadline, 'Callback did not reach ' + state)
            time.sleep(.01)

    def test_truthful_metadata_eight_ranges_and_independent_species(self):
        capabilities = self.request('getCapabilities')
        self.assertEqual(capabilities['implementation'], 'native')
        for method in ('getVoiceLibrary', 'getVoice', 'setVoiceParameter', 'saveVoice', 'revertVoice'):
            self.assertIn(method, capabilities['supportedMethods'])
        for method in ('setVoiceType', 'setMappingEnabled', 'getMappings', 'setMasterGain', 'startRender'):
            self.assertNotIn(method, capabilities['supportedMethods'])
            self.request(method, error='unsupported')
        library = self.request('getVoiceLibrary')
        self.assertEqual(library['maxDefinitions'], 513)
        self.assertEqual(library['editingBehavior'], 'new_notes')
        self.assertEqual([(item['id'], item['proposal']) for item in library['types']], [(name, False) for name in ('struck', 'whistle', 'reed', 'bell', 'air', 'pulse')])
        parameters = {item['id']: item for item in library['parameters']}
        self.assertEqual(set(parameters), {'tuning', 'attack', 'decay', 'gain', 'brightness', 'roughness', 'pulseHz', 'sweepSemitones'})
        expected = {'tuning': (-12, 12, 0, 'linear', 'st'), 'attack': (.002, .100, .008, 'log', 's'),
                    'decay': (.100, 3.000, .650, 'log', 's'), 'gain': (-60, 0, 0, 'linear', 'dB'), 'brightness': (0, 1, .5, 'linear', 'ratio'), 'roughness': (0, 1, 0, 'linear', 'ratio'), 'pulseHz': (0, 16, 0, 'linear', 'Hz'), 'sweepSemitones': (-12, 12, 0, 'linear', 'st')}
        for identity, (minimum, maximum, default, curve, unit) in expected.items():
            spec = parameters[identity]
            self.assertEqual((spec['min'], spec['max'], spec['defaultValue'], spec['curve'], spec['unit']),
                             (minimum, maximum, default, curve, unit))
            self.assertFalse(spec['proposal'])
        self.assertEqual(parameters['tuning']['step'], 1)
        entries = library['entries']
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0]['groupId'], 'native')
        self.assertEqual({item['speciesId'] for item in entries[0]['species']}, {'studio:bird', 'studio:fish', 'studio:other'})
        self.assertEqual(self.voice()['targetId'], 'native')
        default = self.voice()
        self.assertEqual(default['name'], 'Fallback voice')
        self.assertEqual(default['type'], 'struck')
        self.assertIsNone(default['inheritsFromId'])
        self.assertEqual(default['overriddenParameterIds'], [])
        self.assertEqual(default['savedRevision'], '0')
        self.assertFalse(default['dirty'])
        self.edit('gain', .5)
        species = self.voice('species:studio:bird')
        self.assertEqual(species['scope'], 'species')
        self.assertEqual(species['targetId'], 'studio:bird')
        self.assertIsNone(species['inheritsFromId'])
        self.assertEqual(species['values']['gain'], 1)
        self.assertFalse(species['dirty'])
        self.assertEqual(self.request('getAudioState')['masterGainDb'], -18)

    def test_gesture_canonical_values_save_revert_reopen_and_no_autosave(self):
        self.request('getVoiceLibrary')
        identity = 'species:studio:bird'
        self.phase('tuning', .91, 'begin', identity)
        self.save(identity, error='invalid_request')
        self.request('revertVoice', dict(voiceId=identity), error='invalid_request')
        changed = self.phase('tuning', .91, 'end', identity)
        self.assertAlmostEqual(changed['normalizedValue'], 22 / 24)
        self.assertTrue(changed['dirty'])
        self.assertIsInstance(changed['changeRevision'], str)
        voice = self.voice(identity)
        self.assertAlmostEqual(voice['values']['tuning'], 22 / 24)
        self.assertTrue(voice['dirty'])
        saved = self.save(identity)
        self.assertEqual(saved['savedRevision'], '1')
        self.assertFalse(saved['dirty'])
        self.edit('gain', .25, identity)
        restored = self.request('revertVoice', dict(voiceId=identity))
        self.assertEqual(restored['values'], saved['values'])
        self.assertFalse(restored['dirty'])
        self.edit('gain', .5, identity)
        self.restart()
        reopened = self.voice(identity)
        self.assertEqual(reopened['values'], saved['values'])
        self.assertEqual(reopened['savedRevision'], '1')
        self.assertFalse(reopened['dirty'])
        with sqlite3.connect(self.voices) as db:
            self.assertEqual(db.execute('PRAGMA application_id').fetchone()[0], 0x41545653)
            self.assertEqual(db.execute('PRAGMA user_version').fetchone()[0], 2)
            self.assertEqual(db.execute('SELECT count(*) FROM voice_presets').fetchone()[0], 1)
            row = db.execute('SELECT schema_version,saved_revision,tuning,gain FROM voice_presets').fetchone()
            self.assertEqual(row[:2], (2, 1))
            self.assertEqual(row[2], 10)
            self.assertEqual(row[3], 0)

    def test_competing_hosts_compare_and_swap_then_revert_latest(self):
        other = self.new_client()
        self.edit('gain', .5)
        self.edit('gain', .25, client=other)
        saved = self.save()
        self.assertEqual(saved['savedRevision'], '1')
        self.save(client=other, error='stale_generation')
        conflict = self.voice(client=other)
        self.assertEqual(conflict['savedRevision'], '0')
        self.assertEqual(conflict['values']['gain'], .25)
        self.assertTrue(conflict['dirty'])
        restored = other.request('revertVoice', dict(voiceId='default'))
        self.assertEqual(restored['savedRevision'], '1')
        self.assertEqual(restored['values']['gain'], .5)
        self.assertFalse(restored['dirty'])
        self.edit('gain', .75, client=other)
        self.assertEqual(self.save(client=other)['savedRevision'], '2')
        self.assertEqual(self.request('revertVoice', dict(voiceId='default'))['values']['gain'], .75)

    def test_controls_preserve_preparation_generation_position_and_driver(self):
        self.prepare()
        self.request('setAudioEnabled', dict(enabled=True))
        self.transport('play')
        time.sleep(.04)
        before = self.request('getTransport')
        self.assertGreater(int(before['positionUs']), 0)
        starts = self.starts.read_text()
        for parameter, value in (('tuning', 1), ('attack', 0), ('decay', 1), ('gain', .5)):
            self.edit(parameter, value, 'species:studio:bird')
        self.save('species:studio:bird')
        self.edit('gain', .1, 'species:studio:bird')
        self.request('revertVoice', dict(voiceId='species:studio:bird'))
        after = self.request('getTransport')
        self.assertEqual(after['preparationId'], before['preparationId'])
        self.assertEqual(after['generation'], before['generation'])
        self.assertEqual(after['durationUs'], before['durationUs'])
        self.assertEqual(after['state'], 'playing')
        self.assertGreaterEqual(int(after['positionUs']), int(before['positionUs']))
        self.assertEqual(self.request('getAudioState')['status'], 'running')
        self.assertEqual(self.starts.read_text(), starts, 'Voice edit restarted output driver')
        self.transport('pause')
        paused = self.wait_state('paused')
        self.edit('gain', .6)
        self.save()
        self.request('revertVoice', dict(voiceId='default'))
        self.assertEqual(self.request('getTransport'), paused, 'Voice edit changed paused transport')
        self.assertEqual(self.starts.read_text(), starts)

    def test_failed_preparation_preserves_old_voice_slot_associations(self):
        self.request('prepareReplay', dict(sessionId=self.only_bird['id'], basis='observed',
            speedNumerator='1', speedDenominator='1'))
        original = self.request('getTransport')
        self.request('setAudioEnabled', dict(enabled=True))
        self.client.raw('fixture:reset-peak')
        self.transport('play')
        time.sleep(.06)
        baseline = self.client.raw('fixture:peak')['peak']
        self.assertGreater(baseline, .001, 'Synthetic single-species reference produced no PCM')
        self.request('setTransport', dict(action='panic'))
        self.request('prepareReplay', dict(sessionId=self.too_long['id'], basis='observed',
            speedNumerator='1', speedDenominator='1'), error='invalid_request')
        retained = self.request('getTransport')
        self.assertEqual(retained['preparationId'], original['preparationId'])
        self.assertEqual(retained['durationUs'], original['durationUs'])
        self.assertEqual(retained['state'], 'stopped')
        self.assertEqual(self.request('getAudioState')['status'], 'disabled')
        # B's species occupies the same numerical slot A would use in a newly
        # built plan. Failed B preparation must not publish that replacement map.
        self.edit('gain', 0, 'species:studio:other')
        self.assertEqual(self.voice('species:studio:other')['values']['gain'], 0)
        after = self.request('getTransport')
        self.assertEqual(after['preparationId'], retained['preparationId'])
        self.assertEqual(after['generation'], retained['generation'])
        self.request('setAudioEnabled', dict(enabled=True))
        self.client.raw('fixture:reset-peak')
        self.transport('play')
        time.sleep(.06)
        unaffected = self.client.raw('fixture:peak')['peak']
        self.assertGreater(unaffected, baseline * .5, 'Editing rejected B wrongly changed retained A PCM')
        self.request('setTransport', dict(action='panic'))
        self.edit('gain', 0, 'species:studio:bird')
        self.client.raw('fixture:reset-peak')
        self.transport('play')
        time.sleep(.06)
        muted = self.client.raw('fixture:peak')['peak']
        self.assertLess(muted, baseline * .005, 'Retained A species edit no longer reached its voice slot')

    def test_failed_end_closes_gesture_and_preserves_last_acknowledged_value(self):
        self.prepare()
        self.request('setAudioEnabled', dict(enabled=True))
        self.transport('play')
        identity = 'species:studio:bird'
        self.phase('gain', .5, 'begin', identity)
        self.phase('gain', .4, 'change', identity)
        self.assertEqual(self.client.raw('fixture:stall'), {'fixture': 'stalled'})
        self.phase('gain', .1, 'end', identity, error='unavailable')
        authoritative = self.voice(identity)
        self.assertEqual(authoritative['values']['gain'], .4)
        self.assertTrue(authoritative['dirty'])
        self.assertEqual(self.request('getAudioState')['status'], 'disabled')
        self.assertEqual(self.save(identity)['savedRevision'], '1')
        self.request('revertVoice', dict(voiceId=identity))
        self.edit('gain', .7, identity)
        self.assertEqual(self.voice(identity)['values']['gain'], .7)

    def test_invalid_requests_leave_draft_and_transport_unchanged(self):
        self.prepare()
        before = self.voice()
        transport = self.request('getTransport')
        valid = dict(voiceId='default', parameterId='gain', normalizedValue=.5, gesture='begin')
        cases = [('getVoiceLibrary', dict(extra=0)), ('getVoice', dict(voiceId='unknown')),
                 ('getVoice', dict(voiceId='default', extra=0)),
                 ('setVoiceParameter', dict(valid, parameterId='release')),
                 ('setVoiceParameter', dict(valid, normalizedValue=-.1)),
                 ('setVoiceParameter', dict(valid, normalizedValue=1.1)),
                 ('setVoiceParameter', dict(valid, normalizedValue='0.5')),
                 ('setVoiceParameter', dict(valid, normalizedValue=True)),
                 ('setVoiceParameter', dict(valid, normalizedValue=None)),
                 ('setVoiceParameter', dict(valid, gesture='end')),
                 ('setVoiceParameter', dict(valid, gesture='change')),
                 ('setVoiceParameter', dict(valid, gesture='invalid')),
                 ('setVoiceParameter', dict(valid, voiceId='unknown')),
                 ('setVoiceParameter', dict(valid, extra=0)),
                 ('saveVoice', dict(voiceId='default', expectedRevision=0)),
                 ('saveVoice', dict(voiceId='default', expectedRevision='-1')),
                 ('saveVoice', dict(voiceId='default', expectedRevision='9223372036854775808')),
                 ('saveVoice', dict(voiceId='default', expectedRevision='0', extra=0)),
                 ('revertVoice', dict(voiceId='default', extra=0))]
        for method, args in cases:
            with self.subTest(method=method, args=args):
                self.request(method, args, error='invalid_request')
        for literal in ('1e999', '-1e999', 'NaN', 'null', 'true'):
            envelope = '{"protocolVersion":1,"requestId":"bad-real","method":"setVoiceParameter","args":{"voiceId":"default","parameterId":"gain","gesture":"begin","normalizedValue":' + literal + '}}'
            reply = self.client.raw(envelope)
            self.assertFalse(reply['ok'])
            self.assertEqual(reply['error']['code'], 'invalid_request')
        self.assertEqual(self.voice(), before)
        self.assertEqual(self.request('getTransport'), transport)


if __name__ == '__main__':
    unittest.main()
