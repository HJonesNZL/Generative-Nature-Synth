"""Disposable-library M2 render checks; independent equations, no providers/devices."""
import hashlib
import json
import math
from datetime import datetime, timedelta, timezone
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

BINARY = Path(sys.argv.pop(1)).resolve()
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from animal_atlas.model import event, stamp
from animal_atlas.store import Store
from animal_atlas.sessions import Sessions
from audio_oracle import read_wave, sample

START = datetime(2026, 1, 1, tzinfo=timezone.utc)


def timestamp(microseconds):
    return stamp(START + timedelta(microseconds=microseconds))


class NativeAudioTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.folder = Path(self.temp.name)
        self.path = self.folder / 'library.sqlite3'
        self.store = Store(self.path)
        self.sessions = Sessions(self.store)
        self.sequence = 0
        self.output_sequence = 0

    def tearDown(self):
        self.temp.cleanup()

    def snapshot(self, offsets=(100001,), species=None, end_us=1000000, name='Synthetic audio fixture'):
        records = []
        for offset in offsets:
            self.sequence += 1
            value = event('fixture', str(self.sequence), timestamp(offset),
                          'acoustic_species_detection', 'station_detection', -41, 174,
                          station_id='fixture:station:one', species_id=species,
                          attribution='Synthetic development fixture; not a wildlife observation',
                          provenance_mode='demo')
            records.append((value, {'private_raw': 'never export provider raw data'}))
        with patch('animal_atlas.store.stamp', side_effect=lambda value=None: stamp(value) if value is not None else timestamp(2000000)):
            self.store.ingest('fixture', records, True)
        saved = self.sessions.snapshot(name, sources=['fixture'] if records else [],
                                       observed_from=timestamp(0), observed_to=timestamp(end_us))
        self.assertEqual(saved['event_count'], len(records), 'Fixture lost source records')
        return saved

    def render(self, saved, *options, output=None, ok=True):
        self.output_sequence += 1
        output = output if output is not None else self.folder / ('render-%d.wav' % self.output_sequence)
        command = [str(BINARY), 'render', '--db', str(self.path), '--session', saved['id'],
                   '--output', str(output)] + list(options)
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        if ok:
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(output.is_file())
            self.assertFalse(Path(str(output) + '.rendering').exists())
            return output, json.loads(result.stdout)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertEqual(result.stdout, '')
        return output, result.stderr

    def assert_oracle(self, pcm, rate, onset_us=100001, midi=60, attack=.008, decay=.650, gain_db=0, voices=1):
        onset = (onset_us*rate + 999999)//1000000
        frequency = 440*2**((midi-69)/12)
        maximum = 0.0
        for frame in range(len(pcm)//2):
            reference = voices*sample(frame-onset, rate, frequency, attack, decay, gain_db)
            maximum = max(maximum, abs(pcm[frame*2]-reference))
            self.assertEqual(pcm[frame*2], pcm[frame*2+1], 'Centred dual mono differs')
        self.assertLessEqual(maximum, 2e-7, 'Direct equation error %.12g' % maximum)
        return maximum

    def test_independent_waveform_recipe_and_source_are_preserved(self):
        saved = self.snapshot(name='Fixture "quoted" • Tūī')
        before = self.path.read_bytes()
        before_rows = self.sessions.rows(saved['id'])
        output, result = self.render(saved)
        rate, pcm, recipe, raw = read_wave(output)
        self.assertEqual(rate, 48000)
        self.assertEqual(len(raw), 48000*6)
        self.assert_oracle(pcm, rate)
        self.assertEqual(recipe, result['recipe'])
        version = subprocess.run([str(BINARY), 'version'], capture_output=True, text=True, check=True)
        self.assertIn(recipe['engine_source_sha256'], version.stdout)
        self.assertRegex(recipe['engine_source_sha256'], r'^[0-9a-f]{64}$')
        self.assertEqual(recipe['source_content_sha256'], saved['content_hash'])
        self.assertEqual(recipe['metadata_integrity'], 'sha256-envelope-v1-verified')
        self.assertEqual(recipe['session_name'], saved['name'])
        self.assertEqual(recipe['input_events'], 1)
        self.assertEqual(recipe['admitted_missing_species'], 1)
        self.assertEqual(recipe['frames'], '48000')
        self.assertEqual(recipe['synthetic_audio'], True)
        self.assertEqual(recipe['dither'], False)
        self.assertFalse(recipe['capture_truncated'])
        self.assertEqual((result['triggered'], result['stolen'], result['faults']), (1, 0, 0))
        self.assertGreater(result['peak'], .01)
        self.assertLess(result['peak'], .05)
        self.assertNotIn(b'never export provider raw data', output.read_bytes())
        self.assertEqual(self.path.read_bytes(), before)
        self.assertEqual(self.sessions.rows(saved['id']), before_rows)
        self.assertEqual(before_rows[0]['event']['provenance_mode'], 'demo')
        # An off-by-one onset is a substantial mismatch, not a tolerance loophole.
        frequency = 440*2**((60-69)/12)
        onset = 4801
        shifted_error = max(abs(pcm[2*n]-sample(n-onset-1, rate, frequency))
                            for n in range(onset, onset+1000))
        self.assertGreater(shifted_error, 1e-4)

    def test_rate_and_parameter_extremes_against_direct_equations(self):
        saved = self.snapshot(end_us=1000001)
        cases = [(22050, 48, 2, 100, -60), (44100, 72, 100, 3000, 0),
                 (48000, 60, 8, 650, -12), (96000, 72, 2, 100, 0),
                 (192000, 48, 100, 100, -6)]
        for rate, root, attack, decay, gain in cases:
            with self.subTest(rate=rate, root=root, attack=attack, decay=decay, gain=gain):
                output, result = self.render(saved, '--sample-rate', str(rate), '--root-midi', str(root),
                                             '--attack-ms', str(attack), '--decay-ms', str(decay), '--gain-db', str(gain))
                actual_rate, pcm, recipe, _ = read_wave(output)
                self.assertEqual(actual_rate, rate)
                self.assert_oracle(pcm, rate, midi=root, attack=attack/1000, decay=decay/1000, gain_db=gain)
                expected_frames = max((1000001*rate+999999)//1000000,
                                      (100001*rate+999999)//1000000 + math.ceil(attack/1000*rate)
                                      + math.ceil(decay/1000*rate) + math.ceil(.020*rate))
                self.assertEqual(len(pcm)//2, expected_frames)
                self.assertEqual(int(recipe['frames']), expected_frames)
                self.assertEqual(result['faults'], 0)

    def test_species_utf8_mapping_is_stable_and_does_not_claim_animal_audio(self):
        species = 'fixture:species:tūī'
        saved = self.snapshot(species=species)
        value = 2166136261
        for byte in species.encode('utf-8'):
            value = ((value ^ byte)*16777619) & 0xffffffff
        midi = 60 + [0, 2, 4, 7, 9, 12, 14, 16][value % 8]
        output, result = self.render(saved)
        rate, pcm, recipe, _ = read_wave(output)
        self.assert_oracle(pcm, rate, midi=midi)
        self.assertEqual(recipe['admitted_missing_species'], 0)
        self.assertEqual(recipe['pitch_mapping'], 'fnv1a32-species-pentatonic-v1')
        self.assertTrue(recipe['synthetic_audio'])
        self.assertEqual(result['faults'], 0)

    def test_density_admission_and_pcm_are_block_partition_independent(self):
        saved = self.snapshot(offsets=tuple(100001 for _ in range(15)))
        first_bytes = None
        for block in (1, 7, 64, 127, 512, 4096):
            with self.subTest(block=block):
                output, result = self.render(saved, '--block-frames', str(block))
                rate, pcm, recipe, _ = read_wave(output)
                self.assertEqual(recipe['admission'], 'first-4-per-scaled-10ms-v1')
                self.assertEqual((recipe['input_events'], recipe['admitted'], recipe['suppressed_density']), (15, 4, 11))
                self.assertEqual((result['triggered'], result['stolen'], result['faults']), (4, 0, 0))
                self.assert_oracle(pcm, rate, voices=4)
                if first_bytes is None:
                    first_bytes = output.read_bytes()
                else:
                    self.assertEqual(output.read_bytes(), first_bytes)

    def test_voice_stealing_stays_bounded_and_partition_invariant(self):
        offsets = tuple(100000 + window*10000 for window in range(12) for _ in range(4))
        saved = self.snapshot(offsets=offsets)
        reference = None
        for block in (1, 7, 127, 4096):
            with self.subTest(block=block):
                output, result = self.render(saved, '--block-frames', str(block))
                _, pcm, recipe, _ = read_wave(output)
                self.assertEqual((recipe['admitted'], recipe['suppressed_density']), (48, 0))
                self.assertEqual((result['triggered'], result['stolen'], result['faults']), (48, 40, 0))
                self.assertLessEqual(max(map(abs, pcm)), .8)
                self.assertTrue(all(math.isfinite(value) for value in pcm))
                if reference is None:
                    reference = output.read_bytes()
                else:
                    self.assertEqual(output.read_bytes(), reference)

    def test_speed_compresses_time_but_not_pitch_and_arrival_mode_is_explicit(self):
        saved = self.snapshot(offsets=(200000,), end_us=1000000)
        output, result = self.render(saved, '--speed', '2')
        rate, pcm, recipe, _ = read_wave(output)
        self.assert_oracle(pcm, rate, onset_us=100000)
        self.assertEqual((recipe['speed_numerator'], recipe['speed_denominator']), ('2', '1'))
        self.assertEqual(len(pcm)//2, 4800+math.ceil(.008*rate)+math.ceil(.650*rate)+math.ceil(.020*rate))
        arrived, _ = self.render(saved, '--mode', 'arrival')
        arrival_rate, arrival_pcm, arrival_recipe, _ = read_wave(arrived)
        self.assertEqual(arrival_recipe['mode'], 'arrival')
        self.assert_oracle(arrival_pcm, arrival_rate, onset_us=0)

    def test_empty_windows_are_exact_silence_without_a_synthetic_tail(self):
        saved = self.snapshot(offsets=(), end_us=1000000)
        output, result = self.render(saved, '--speed', '2')
        rate, pcm, recipe, raw = read_wave(output)
        self.assertEqual(rate, 48000)
        self.assertEqual(len(pcm), 24000*2)
        self.assertEqual(raw, bytes(24000*6))
        self.assertEqual((recipe['input_events'], recipe['admitted'], result['triggered'], result['peak']), (0, 0, 0, 0))

    def test_tail_extends_only_when_needed_and_zeroes_after_lifetime(self):
        saved = self.snapshot(offsets=(999999,), end_us=1000000)
        output, _ = self.render(saved)
        rate, pcm, recipe, _ = read_wave(output)
        expected = 48000+384+31200+960
        self.assertEqual(int(recipe['frames']), expected)
        self.assertEqual(len(pcm)//2, expected)
        self.assert_oracle(pcm, rate, onset_us=999999)
        self.assertEqual(pcm[-2:], [0, 0])

    def test_existing_file_database_and_symlink_are_never_replaced(self):
        saved = self.snapshot()
        output = self.folder / 'existing.wav'
        output.write_bytes(b'owner content')
        self.render(saved, output=output, ok=False)
        self.assertEqual(output.read_bytes(), b'owner content')
        before = self.path.read_bytes()
        self.render(saved, output=self.path, ok=False)
        self.assertEqual(self.path.read_bytes(), before)
        link = self.folder / 'link.wav'
        link.symlink_to(output)
        self.render(saved, output=link, ok=False)
        self.assertTrue(link.is_symlink())
        self.assertEqual(output.read_bytes(), b'owner content')
        dangling = self.folder / 'dangling.wav'
        dangling.symlink_to(self.folder / 'absent.wav')
        self.render(saved, output=dangling, ok=False)
        self.assertTrue(dangling.is_symlink())
        self.assertFalse((self.folder/'absent.wav').exists())

    def test_staging_conflict_and_missing_parent_preserve_existing_state(self):
        saved = self.snapshot()
        output = self.folder / 'staged.wav'
        staging = Path(str(output)+'.rendering')
        staging.mkdir()
        sentinel = staging/'keep.txt'
        sentinel.write_text('existing incomplete render')
        self.render(saved, output=output, ok=False)
        self.assertEqual(sentinel.read_text(), 'existing incomplete render')
        self.assertFalse(output.exists())
        missing = self.folder/'missing'/'render.wav'
        self.render(saved, output=missing, ok=False)
        self.assertFalse(missing.parent.exists())

    def test_corrupt_metadata_or_event_payload_cannot_publish_audio(self):
        saved = self.snapshot()
        with self.store.connect() as db:
            db.execute("UPDATE sessions SET name='tampered' WHERE id=?", (saved['id'],))
        output, _ = self.render(saved, ok=False)
        self.assertFalse(output.exists())
        self.assertFalse(Path(str(output)+'.rendering').exists())
        other = self.sessions.snapshot('Separate sealed fixture', observed_from=timestamp(0), observed_to=timestamp(1000000))
        with self.store.connect() as db:
            db.execute("UPDATE session_events SET event_json='{}' WHERE session_id=?", (other['id'],))
        output, _ = self.render(other, ok=False)
        self.assertFalse(output.exists())

    def test_numeric_and_duration_limits_fail_before_publication(self):
        saved = self.snapshot()
        for key, value in [('--sample-rate', '22049'), ('--sample-rate', '192001'), ('--sample-rate', '48000.5'),
                           ('--root-midi', '47'), ('--root-midi', '73'), ('--root-midi', '-1'),
                           ('--attack-ms', '1.99'), ('--attack-ms', '100.01'), ('--attack-ms', 'nan'),
                           ('--decay-ms', '99'), ('--decay-ms', '3001'), ('--decay-ms', 'inf'),
                           ('--gain-db', '-60.1'), ('--gain-db', '0.1'), ('--gain-db', ' 0'),
                           ('--gain-db', '0junk'), ('--gain-db', '1e999'), ('--block-frames', '0'),
                           ('--block-frames', '4097'), ('--block-frames', '-1'), ('--mode', 'bad'),
                           ('--speed', '0'), ('--unknown', '1')]:
            with self.subTest(key=key, value=value):
                output, _ = self.render(saved, key, value, ok=False)
                self.assertFalse(output.exists())
                self.assertFalse(Path(str(output)+'.rendering').exists())
        too_long = self.snapshot(offsets=(), end_us=1800000001)
        output, _ = self.render(too_long, ok=False)
        self.assertFalse(output.exists())

    def test_riff_reader_rejects_corrupt_extents_and_format(self):
        saved = self.snapshot()
        output, _ = self.render(saved)
        original = bytearray(output.read_bytes())
        for length in (0, 4, 11, len(original)-1):
            path = self.folder/('truncated-%d.wav' % length)
            path.write_bytes(original[:length])
            with self.subTest(length=length), self.assertRaises(ValueError):
                read_wave(path)
        for offset in (4, 20, 40):
            damaged = bytearray(original)
            damaged[offset] ^= 1
            path = self.folder/('invalid-%d.wav' % offset)
            path.write_bytes(damaged)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                read_wave(path)


if __name__ == '__main__':
    unittest.main()
