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

    def replay(self, expected=0):
        result = subprocess.run([str(BINARY),'replay','--db',str(self.db),'--session',self.session_id],
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
                              provider_scores={'small':1e-07,'large':1e+20,'values':[None,True,2]})
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
