"""Disposable native storage/CLI tests; no owner state or providers."""
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()


class NativeStorageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.db = Path(self.temp.name) / 'fixture.sqlite3'
        self.session_id = 'fixture'
        self.events = [dict(schema_version=1, event_id='fixture:one', source_id='fixture',
            event_kind='acoustic_species_detection', location_kind='station_detection',
            provider_event_id='one',coordinate_order='latitude,longitude',provenance_mode='live_source',
            species_id=None, individual_id=None, deployment_id=None, station_id='fixture:station',
            latitude=-41.0, longitude=174, observed_at_utc='2026-01-01T00:00:00Z',
            first_seen_at_utc='2026-01-01T00:00:00Z', ingested_at_utc='2026-01-01T00:00:00Z')]
        self.write_fixture()

    def tearDown(self):
        self.temp.cleanup()

    def write_fixture(self, sequence=1, truncated=False):
        encode = lambda value: json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False)
        rows = [dict(event=value, original_seq=sequence+i) for i, value in enumerate(self.events)]
        checksum = hashlib.sha256(''.join(encode(row)+'\n' for row in rows).encode()).hexdigest()
        with sqlite3.connect(self.db) as db:
            db.executescript('''PRAGMA user_version=2;
                DROP TABLE IF EXISTS sessions; DROP TABLE IF EXISTS session_events;
                CREATE TABLE sessions(id TEXT,name TEXT,state TEXT,origin TEXT,event_count INTEGER,
                    options_json TEXT,content_hash TEXT,created_at TEXT);
                CREATE TABLE session_events(session_id TEXT,ordinal INTEGER,original_seq INTEGER,event_json TEXT);''')
            db.execute('INSERT INTO sessions VALUES (?,?,?,?,?,?,?,?)',
                ('fixture','Fixture','frozen','archive_snapshot',len(rows),
                 encode(dict(capture_truncated=truncated)),checksum,'2026-01-01'))
            db.executemany('INSERT INTO session_events VALUES (?,?,?,?)',
                [('fixture',i,row['original_seq'],encode(row['event'])) for i,row in enumerate(rows)])

    def replay(self, expected=0, mode="observed", speed="1"):
        result = subprocess.run([str(BINARY),'replay','--db',str(self.db),'--session',self.session_id,'--mode',mode,'--speed',speed],
                                capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,expected,result.stderr or result.stdout)
        if expected:
            self.assertEqual(result.stdout,'')
            return result.stderr
        return json.loads(result.stdout)

    def test_valid_content_and_explicit_verification(self):
        before = self.db.read_bytes()
        result = self.replay()
        self.assertEqual(result['integrity'],'sha256-event-content-verified; legacy-metadata-unverified')
        self.assertEqual(self.db.read_bytes(),before)

    def test_actual_python_writer_and_bundle_import_verify(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
        from animal_atlas.model import event
        from animal_atlas.store import Store
        from animal_atlas.sessions import Sessions
        root = Path(self.temp.name)
        self.db = root / 'writer.sqlite3'
        store = Store(self.db)
        value = event('birds','one','2026-01-01T00:00:00Z',
                      'acoustic_species_detection','station_detection',-41.0,174,
                      common_name='Tūī 🐦')
        store.ingest('birds',[(value,{})],True)
        sessions = Sessions(store)
        self.session_id = sessions.snapshot('Writer fixture')['id']
        original = self.replay()['events']
        bundle = root / 'portable.atlas.json'
        sessions.export(self.session_id,bundle)
        self.db = root / 'imported.sqlite3'
        imported = Sessions(Store(self.db))
        self.session_id = imported.import_bundle(bundle)['id']
        self.assertEqual(self.replay()['events'],original)

    def test_native_metadata_categories_and_envelope_versions(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
        from animal_atlas.store import Store
        from animal_atlas.sessions import Sessions
        self.db = Path(self.temp.name) / 'sealed.sqlite3'
        store = Store(self.db)
        original = Sessions(store).snapshot('Tūī 🐦', seed=3)
        self.session_id = original['id']
        self.assertIn('sha256-envelope-v1-verified',self.replay()['integrity'])
        changes = dict(name='other',state='interrupted',created_at='2000-01-01T00:00:00Z',
            ended_at=None,start_seq=3,end_seq=8,intended_end_at='2000-01-01T00:00:00Z',
            origin='other',event_count=1,content_hash='0'*64,options_json='{}',
            envelope_version=2,envelope_json=None,envelope_hash='0'*64)
        for field,value in changes.items():
            with self.subTest(field=field):
                with store.connect() as db:
                    previous=db.execute('SELECT '+field+' FROM sessions').fetchone()[0]
                    db.execute('UPDATE sessions SET '+field+'=?',(value,))
                self.replay(expected=1)
                with store.connect() as db: db.execute('UPDATE sessions SET '+field+'=?',(previous,))
        with store.connect() as db:
            db.execute('UPDATE sessions SET envelope_version=0,envelope_json=NULL,envelope_hash=NULL')
        self.assertIn('legacy-metadata-unverified',self.replay()['integrity'])

    def test_shared_semantic_corpus(self):
        corpus=json.loads((Path(__file__).parent/'fixtures/event_semantics_v1.json').read_text())
        for case in corpus['cases']:
            value=dict(corpus['base'], **case['changes'])
            for key in case['remove']: value.pop(key)
            with self.subTest(case=case['name']):
                self.events=[value]
                self.write_fixture()
                self.replay(expected=0 if case['accepted'] else 1)

    def test_duplicate_event_keys_rejected_with_matching_hash(self):
        payload=json.dumps(self.events[0],sort_keys=True,separators=(',',':'))
        payload=payload[:-1]+',"schema_version":1}'
        checksum=hashlib.sha256(('{"event":'+payload+',"original_seq":1}\n').encode()).hexdigest()
        with sqlite3.connect(self.db) as db:
            db.execute('UPDATE session_events SET event_json=?',(payload,))
            db.execute('UPDATE sessions SET content_hash=?',(checksum,))
        self.replay(expected=1)

    def test_requested_empty_trimmed_and_truncated_window_parity(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
        from unittest.mock import patch
        from animal_atlas.store import Store
        from animal_atlas.sessions import Sessions
        from animal_atlas.model import event
        from animal_atlas.replay import Timeline
        self.db=Path(self.temp.name)/'window.sqlite3'
        store=Store(self.db); sessions=Sessions(store)
        bounds=dict(observed_from='1900-01-01T00:00:00Z',observed_to='1900-01-01T01:00:00Z')
        self.session_id=sessions.snapshot('Empty hour',**bounds)['id']
        self.assertEqual(self.replay()['duration_us'],3600000000)
        self.session_id=sessions.snapshot('Empty trim',window_policy='trim_to_material',**bounds)['id']
        self.assertEqual(self.replay()['duration_us'],0)
        for identity,time in (('first','00:10:00'),('last','00:50:00'),('excluded','01:00:00')):
            value=event('birds',identity,'1900-01-01T'+time+'Z','acoustic_species_detection','station_detection')
            store.ingest('birds',[(value,{})],True)
        for policy,expected_due,expected_duration in (
            ('preserve_requested',[600000000,3000000000],3600000000),
            ('trim_to_material',[0,2400000000],2400000001)):
            with self.subTest(policy=policy):
                self.session_id=sessions.snapshot('Historical hour',window_policy=policy,**bounds)['id']
                result=self.replay()
                self.assertEqual(result['duration_us'],expected_duration)
                self.assertEqual([row['due_us'] for row in result['events']],expected_due)
                self.assertEqual(result['observed_window_policy'],policy)
                python=Timeline(sessions,self.session_id)
                self.assertEqual(result['events'],python.items)
                self.assertEqual(result['duration_us'],python.duration_us)
                for mode in ('observed','arrival'):
                    scaled=self.replay(mode=mode,speed='60')
                    oracle=Timeline(sessions,self.session_id,mode,speed='60')
                    self.assertEqual(scaled['events'],oracle.items)
                    self.assertEqual(scaled['duration_us'],oracle.duration_us)
        with patch('animal_atlas.sessions.MAX_EVENTS',1):
            self.session_id=sessions.snapshot('Truncated hour',**bounds)['id']
        result=self.replay()
        self.assertTrue(result['capture_truncated'])
        self.assertEqual(result['duration_us'],3600000000)
        from animal_atlas.model import canonical
        from animal_atlas.integrity import projection
        from animal_atlas.sessions import content_hash
        metadata,rows=sessions.frozen(self.session_id)
        rows[0]['event']['observed_at_utc']=bounds['observed_to']
        metadata['content_hash']=content_hash(rows)
        payload=canonical({'version':1,'session':projection(metadata)})
        with store.connect() as db:
            db.execute('UPDATE session_events SET event_json=? WHERE session_id=?',
                       (canonical(rows[0]['event']),self.session_id))
            db.execute('UPDATE sessions SET content_hash=?,envelope_json=?,envelope_hash=? WHERE id=?',
                (metadata['content_hash'],payload,hashlib.sha256(payload.encode()).hexdigest(),self.session_id))
        self.assertIn('outside requested',self.replay(expected=1))
        with self.assertRaises(ValueError): Timeline(sessions,self.session_id)

    def test_invalid_options_rejected_even_with_recomputed_envelope(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
        from animal_atlas.store import Store
        from animal_atlas.sessions import Sessions
        from animal_atlas.model import canonical
        from animal_atlas.integrity import projection
        self.db=Path(self.temp.name)/'invalid-options.sqlite3'
        store=Store(self.db); sessions=Sessions(store)
        original=sessions.snapshot('Options')
        self.session_id=original['id']
        changes=[{'seed':True},{'seed':-1},{'seed':2**63},{'event_schema_version':2},
                 {'capture_truncated':1},{'sources':'birds'},{'sources':[7]},
                 {'window_policy_version':2},{'window_policy':'preserve_requested'},
                 {'window_policy':'unknown','window_policy_version':1},
                 {'timeline_bounds':{'observed':[], 'arrival':[]}}, {'unknown_field':5}]
        for change in changes:
            with self.subTest(change=change):
                metadata=projection(original); metadata['options']=dict(original['options'],**change)
                payload=canonical({'version':1,'session':metadata})
                with store.connect() as db:
                    db.execute('UPDATE sessions SET options_json=?,envelope_json=?,envelope_hash=?',
                        (canonical(metadata['options']),payload,hashlib.sha256(payload.encode()).hexdigest()))
                self.replay(expected=1)
                with self.assertRaises(ValueError): sessions.get(self.session_id)

    def test_native_interrupted_import_and_overflow_rejection(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
        from animal_atlas.store import Store
        from animal_atlas.sessions import Sessions
        from animal_atlas.model import event
        self.db=Path(self.temp.name)/'interrupted.sqlite3'
        store=Store(self.db); sessions=Sessions(store)
        recording=sessions.start('Interrupted')
        for identity in ('baseline','during'):
            value=event('birds',identity,'2026-01-01T00:00:00Z','acoustic_species_detection','station_detection')
            store.ingest('birds',[(value,{})],True)
        self.session_id=sessions.stop(recording['id'],interrupted=True)['id']
        expected=self.replay()['events']
        self.assertEqual(len(expected),1)
        path=Path(self.temp.name)/'interrupted.json'; sessions.export(self.session_id,path)
        imported=sessions.import_bundle(path); self.session_id=imported['id']
        self.assertEqual(imported['state'],'interrupted')
        self.assertEqual(self.replay()['events'],expected)
        self.session_id=sessions.snapshot('Calendar range',sources=[],observed_from='0001-01-01T00:00:00Z',
                                        observed_to='9999-12-31T23:59:59Z')['id']
        self.assertIn('overflow',self.replay(expected=1,speed='0.01'))

    def test_modified_payload_rejected(self):
        with sqlite3.connect(self.db) as db:
            db.execute("UPDATE session_events SET event_json=replace(event_json,'-41.0','-42.0')")
        self.assertIn('integrity',self.replay(expected=1))

    def test_modified_sequence_rejected(self):
        with sqlite3.connect(self.db) as db:
            db.execute('UPDATE session_events SET original_seq=2')
        self.assertIn('integrity',self.replay(expected=1))

    def test_absent_or_invalid_checksum_rejected(self):
        for value in (None,'','a'*63,'g'*64,'0'*64):
            with self.subTest(checksum=value):
                with sqlite3.connect(self.db) as db:
                    db.execute('UPDATE sessions SET content_hash=?',(value,))
                self.assertIn('integrity',self.replay(expected=1))

    def test_unicode_numeric_edges_nested_values_and_large_sequence(self):
        self.events[0].update(common_name='Tūī 🐦 "quoted"\n', latitude=-0.0,
                              provider_scores={'small':1e-07,'large':1e+20,'unknown':None})
        self.write_fixture(sequence=9007199254740993)
        result=self.replay()
        self.assertEqual(result['events'][0]['event'],self.events[0])
        self.assertEqual(result['events'][0]['original_seq'],9007199254740993)

    def test_empty_session_verifies(self):
        self.events=[]
        self.write_fixture()
        self.assertEqual(self.replay()['event_count'],0)

    def test_truncation_visible_in_replay(self):
        self.write_fixture(truncated=True)
        self.assertTrue(self.replay()['capture_truncated'])

    def test_equivalent_reformatted_storage_fails_closed(self):
        with sqlite3.connect(self.db) as db:
            db.execute('UPDATE session_events SET event_json=?',(json.dumps(self.events[0],indent=2),))
        self.assertIn('integrity',self.replay(expected=1))

    def test_count_and_ordinal_corruption_rejected(self):
        for sql in ('UPDATE sessions SET event_count=2','UPDATE session_events SET ordinal=1'):
            with self.subTest(sql=sql):
                self.write_fixture()
                with sqlite3.connect(self.db) as db: db.execute(sql)
                self.replay(expected=1)

    def test_json_text_types_rejected(self):
        self.events[0]['species_id']=123
        self.write_fixture()
        self.replay(expected=1)

    def test_missing_database_is_not_created(self):
        self.db.unlink()
        self.replay(expected=1)
        self.assertFalse(self.db.exists())


if __name__=='__main__':
    unittest.main()
