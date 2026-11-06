import copy
import hashlib
import json
import sqlite3
from unittest.mock import patch
from animal_atlas.model import canonical
from animal_atlas.sessions import Sessions
from animal_atlas.store import Store
from animal_atlas.replay import Timeline
from test_sessions import SessionCase, ev


class IntegrityTests(SessionCase):
    def frozen(self):
        self.ingest(ev('a'))
        return self.sessions.snapshot('Tūī 🐦', seed=31,
            observed_from='2025-12-01T00:00:00Z', observed_to='2026-02-01T00:00:00Z')

    def test_each_metadata_category_is_bound(self):
        original = self.frozen()
        changes = dict(name='changed', state='interrupted', created_at='2020-01-01T00:00:00Z',
            ended_at='2020-01-01T00:00:00Z', start_seq=8, end_seq=9,
            intended_end_at='2020-01-01T00:00:00Z', origin='other', event_count=2,
            content_hash='0'*64, options_json=canonical(dict(original['options'], seed=999)))
        for field, value in changes.items():
            with self.subTest(field=field):
                with self.store.connect() as db:
                    db.execute('UPDATE sessions SET '+field+'=?', (value,))
                with self.assertRaises(ValueError):
                    Timeline(self.sessions, original['id'])
                old = canonical(original['options']) if field == 'options_json' else original[field]
                with self.store.connect() as db:
                    db.execute('UPDATE sessions SET '+field+'=?', (old,))

    def test_missing_partial_future_and_corrupt_envelopes(self):
        original = self.frozen()
        for version, payload, checksum in ((1,None,None),(2,original['envelope_json'],original['envelope_hash']),
            (0,original['envelope_json'],None),(1,original['envelope_json'],'0'*64)):
            with self.subTest(version=version, checksum=checksum):
                with self.store.connect() as db:
                    db.execute('UPDATE sessions SET envelope_version=?,envelope_json=?,envelope_hash=?',
                               (version,payload,checksum))
                with self.assertRaises(ValueError): self.sessions.get(original['id'])

    def test_two_roundtrips_preserve_original_context(self):
        original = self.frozen()
        first = None
        for index in range(2):
            path = self.root / ('round'+str(index)+'.json')
            self.sessions.export(original['id'] if index == 0 else restored['id'], path)
            restored = self.sessions.import_bundle(path)
            if first is None: first = copy.deepcopy(restored['options']['original_session'])
            self.assertEqual(restored['options']['original_session'], first)
            self.assertEqual(restored['options']['observed_from'], original['options']['observed_from'])
            self.assertEqual(restored['options']['selection_policy'], original['options']['selection_policy'])
            self.assertEqual(restored['metadata_integrity'],'sha256-envelope-v1-verified')
        self.assertEqual(first['id'],original['id'])
        self.assertEqual(self.store.high_water(),1)

    def test_legacy_v1_and_schema2_are_explicitly_unverified(self):
        original = self.frozen()
        bundle = self.sessions.bundle(original['id'])
        bundle['bundle_version'] = 1
        for key in ('envelope_version','envelope_json','envelope_hash','metadata_integrity'):
            bundle['session'].pop(key)
        bundle.pop('bundle_sha256')
        bundle['bundle_sha256'] = hashlib.sha256(canonical(bundle).encode()).hexdigest()
        path = self.root/'legacy.json'; path.write_text(canonical(bundle))
        restored = self.sessions.import_bundle(path)
        self.assertEqual(restored['options']['original_session']['metadata_integrity'],'legacy-metadata-unverified')
        with self.store.connect() as db:
            for column in ('envelope_version','envelope_json','envelope_hash'):
                db.execute('ALTER TABLE sessions DROP COLUMN '+column)
            db.execute('PRAGMA user_version=2')
        migrated = Sessions(Store(self.root/'db'))
        self.assertEqual(migrated.get(original['id'])['metadata_integrity'],'legacy-metadata-unverified')
        self.assertEqual(migrated.rows(original['id'])[0]['event']['provider_event_id'],'a')

    def test_failed_migration_rolls_back_ddl_and_version(self):
        path = self.root/'broken'
        with sqlite3.connect(path) as db:
            db.executescript('PRAGMA user_version=2; CREATE TABLE sessions(id TEXT,envelope_json TEXT);')
        with self.assertRaises(sqlite3.OperationalError): Store(path)
        with sqlite3.connect(path) as db:
            self.assertEqual(db.execute('PRAGMA user_version').fetchone()[0],2)
            self.assertEqual([r[1] for r in db.execute('PRAGMA table_info(sessions)')],['id','envelope_json'])
            self.assertIsNone(db.execute("SELECT name FROM sqlite_master WHERE name='revisions'").fetchone())

    def test_duplicate_json_and_unknown_options_rejected(self):
        original = self.frozen()
        path = self.root/'duplicate.json'
        path.write_text('{"format":"animal-atlas-session","format":"animal-atlas-session"}')
        with self.assertRaises(ValueError): self.sessions.import_bundle(path)
        bundle = self.sessions.bundle(original['id'])
        bundle['session']['options']['secret'] = 'must not persist'
        bundle.pop('bundle_sha256')
        bundle['bundle_sha256'] = hashlib.sha256(canonical(bundle).encode()).hexdigest()
        path.write_text(canonical(bundle))
        with self.assertRaises(ValueError): self.sessions.import_bundle(path)

    def test_interruption_and_truncation_survive_import(self):
        recording = self.sessions.start('Interrupted')
        self.ingest(ev('baseline'))
        self.ingest(ev('new'), ev('another'))
        with patch('animal_atlas.sessions.MAX_EVENTS',1):
            original = self.sessions.stop(recording['id'],interrupted=True)
        path=self.root/'interrupted.json'; self.sessions.export(original['id'],path)
        imported=self.sessions.import_bundle(path)
        self.assertEqual(imported['state'],'interrupted')
        self.assertTrue(imported['options']['capture_truncated'])
        self.assertEqual(imported['options']['end_time_quality'],original['options']['end_time_quality'])

    def test_frozen_read_rejects_duplicate_keys_and_noncanonical_bytes(self):
        original=self.frozen()
        for payload in (json.dumps(self.sessions.rows(original['id'])[0]['event'],indent=2),
                        '{"schema_version":1,"schema_version":1}'):
            with self.store.connect() as db:
                db.execute('UPDATE session_events SET event_json=?',(payload,))
            with self.assertRaises(ValueError): Timeline(self.sessions,original['id'])
