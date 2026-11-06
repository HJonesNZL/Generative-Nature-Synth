import contextlib
import io
import json
from unittest.mock import patch
from animal_atlas.__main__ import main
from animal_atlas.replay import Timeline
from test_sessions import SessionCase, ev

LOWER='2026-01-01T00:00:00Z'
UPPER='2026-01-01T01:00:00Z'


class WindowTests(SessionCase):
    def test_preserves_leading_trailing_gaps_and_excludes_upper_boundary(self):
        self.ingest(ev('first','2026-01-01T00:10:00Z'),ev('last','2026-01-01T00:50:00Z'),ev('excluded',UPPER))
        session=self.sessions.snapshot('Hour',observed_from=LOWER,observed_to=UPPER)
        replay=Timeline(self.sessions,session['id'],speed='60')
        self.assertEqual(replay.times,[10000000,50000000])
        self.assertEqual(replay.duration_us,60000000)
        self.assertEqual(replay.summary()['observed_window_policy'],'preserve_requested')
        self.assertFalse(replay.summary()['capture_truncated'])
        self.assertEqual(Timeline(self.sessions,session['id'],'arrival').duration_us,1)

    def test_empty_requested_window_is_silent_but_not_an_ecological_claim(self):
        session=self.sessions.snapshot('Empty hour',observed_from=LOWER,observed_to=UPPER)
        replay=Timeline(self.sessions,session['id'])
        self.assertEqual(replay.duration_us,3600000000)
        self.assertEqual(replay.times,[])
        self.assertIn('not evidence of ecological silence',replay.summary()['empty_material_meaning'])
        path=self.root/'empty.json'; self.sessions.export(session['id'],path)
        imported=self.sessions.import_bundle(path)
        self.assertEqual(Timeline(self.sessions,imported['id']).summary(),replay.summary())

    def test_explicit_trim_single_event_and_empty(self):
        self.ingest(ev('one','2026-01-01T00:10:00Z'))
        session=self.sessions.snapshot('Trimmed',observed_from=LOWER,observed_to=UPPER,window_policy='trim_to_material')
        replay=Timeline(self.sessions,session['id'])
        self.assertEqual(replay.times,[0]); self.assertEqual(replay.duration_us,1)
        empty=self.sessions.snapshot('Empty trim',sources=[],observed_from=LOWER,observed_to=UPPER,window_policy='trim_to_material')
        self.assertEqual(Timeline(self.sessions,empty['id']).duration_us,0)

    def test_invalid_policy_and_partial_requests(self):
        for options in (dict(window_policy='preserve_requested'),dict(observed_from=LOWER,window_policy='preserve_requested'),dict(window_policy='x')):
            with self.assertRaises(ValueError): self.sessions.snapshot('Bad',**options)
        partial=self.sessions.snapshot('Partial',observed_from=LOWER)
        self.assertEqual(partial['options']['window_policy'],'trim_to_material')

    def test_truncated_window_stays_full_length_and_is_reported(self):
        self.ingest(ev('one','2026-01-01T00:10:00Z'),ev('two','2026-01-01T00:20:00Z'))
        with patch('animal_atlas.sessions.MAX_EVENTS',1):
            session=self.sessions.snapshot('Capped',observed_from=LOWER,observed_to=UPPER)
        replay=Timeline(self.sessions,session['id'])
        self.assertEqual(replay.duration_us,3600000000)
        self.assertTrue(replay.summary()['capture_truncated'])

    def test_cli_exposes_trim_policy(self):
        from animal_atlas.store import Store
        Store(self.root/"atlas.sqlite3")
        output=io.StringIO()
        with patch('sys.argv',['animal_atlas','--state-dir',str(self.root),'snapshot','--name','CLI',
            '--observed-from',LOWER,'--observed-to',UPPER,'--window-policy','trim_to_material']), contextlib.redirect_stdout(output):
            main()
        self.assertEqual(json.loads(output.getvalue())['options']['window_policy'],'trim_to_material')

    def test_extreme_window_speed_fails_with_explicit_capacity_error(self):
        session=self.sessions.snapshot('Calendar range',observed_from='0001-01-01T00:00:00Z',
                                       observed_to='9999-12-31T23:59:59Z')
        with self.assertRaisesRegex(ValueError,'64-bit'): Timeline(self.sessions,session['id'],speed='0.01')
        with self.assertRaises(ValueError): Timeline(self.sessions,session['id'],speed='1000001/1000000')

    def test_offset_event_selection_uses_utc_and_rejects_false_boundaries(self):
        value=ev('offset')
        value['observed_at_utc']='2026-01-01T12:10:00+12:00'
        self.ingest(value)
        session=self.sessions.snapshot('Offset',observed_from=LOWER,observed_to=UPPER)
        replay=Timeline(self.sessions,session['id'])
        self.assertEqual(replay.times,[600000000])
        self.assertEqual(replay.items[0]['event']['observed_at_utc'],value['observed_at_utc'])
        for boundary in ('',False,0):
            with self.subTest(boundary=boundary), self.assertRaises(ValueError):
                self.sessions.snapshot('Invalid',observed_from=boundary,observed_to=UPPER)

    def test_import_rejects_out_of_window_event_before_persisting(self):
        import hashlib
        from animal_atlas.integrity import projection
        from animal_atlas.model import canonical
        from animal_atlas.sessions import content_hash
        self.ingest(ev('inside','2026-01-01T00:10:00Z'))
        session=self.sessions.snapshot('Bounded',observed_from=LOWER,observed_to=UPPER)
        bundle=self.sessions.bundle(session['id'])
        bundle['events'][0]['event']['observed_at_utc']=UPPER
        original=bundle['session']
        original['content_hash']=content_hash(bundle['events'])
        original['envelope_json']=canonical({'version':1,'session':projection(original)})
        original['envelope_hash']=hashlib.sha256(original['envelope_json'].encode()).hexdigest()
        bundle.pop('bundle_sha256'); bundle['bundle_sha256']=hashlib.sha256(canonical(bundle).encode()).hexdigest()
        path=self.root/'outside.json'; path.write_text(canonical(bundle))
        with self.assertRaisesRegex(ValueError,'outside requested'): self.sessions.import_bundle(path)
        self.assertEqual(len(self.sessions.list()),1)
