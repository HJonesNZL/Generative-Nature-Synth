"""Isolated audit probes; no owner database, API, or provider access."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from animal_atlas import __main__ as cli
from animal_atlas.model import event, canonical
from animal_atlas.store import Store
from animal_atlas.sessions import Sessions, content_hash
from animal_atlas.replay import Timeline
BINARY = str(Path(__file__).resolve().parents[2] / 'build/native-audit-release/atlas-native')
results = {}

def ev(i, source='test'):
    return event(source, i, '2026-01-01T02:00:00Z', 'acoustic_species_detection', 'station_detection', -41, 174, station_id=source+':station:one')

with tempfile.TemporaryDirectory(prefix='atlas-astra-audit-') as tmp:
    root = Path(tmp)
    recovery = root / 'recovery'
    recovery.mkdir()
    store = Store(recovery / 'atlas.sqlite3')
    sessions = Sessions(store)
    store.ingest('birdweather', [(ev('baseline', 'birdweather'), {})], True)
    recording = sessions.start('Abandoned prior-process recording')
    store.ingest('birdweather', [(ev('before-crash', 'birdweather'), {})], True)
    config = root / 'config.json'
    config.write_text(json.dumps({'birdweather': {'enabled': True}, 'nzsharks': {'enabled': False}}))
    def fixture_poll(self, sid):
        return self.store.ingest(sid, [(ev('after-crash-new-command', sid), {})], True), 0
    with patch.object(sys, 'argv', ['animal_atlas', '--state-dir', str(recovery), 'collect-once', '--config', str(config)]), patch('animal_atlas.__main__.Collector.poll', fixture_poll), contextlib.redirect_stdout(io.StringIO()):
        cli.main()
    state_after = sessions.get(recording['id'])['state']
    sessions.recover()
    results['A1_recovery'] = {'state_after_collect_once': state_after, 'recovered_event_ids': [r['event']['provider_event_id'] for r in sessions.rows(recording['id'])]}

    store = Store(root / 'original.db')
    sessions = Sessions(store)
    store.ingest('test', [(ev('one'), {})], True)
    saved = sessions.snapshot('History', observed_from='2026-01-01T00:00:00Z', observed_to='2026-01-01T04:00:00Z')
    bundle = sessions.bundle(saved['id'])
    path = root / 'piece.atlas.json'
    path.write_text(canonical(bundle))
    restored_sessions = Sessions(Store(root / 'restored.db'))
    restored = restored_sessions.import_bundle(path)
    results['A3_query'] = {k: {'before': saved['options'].get(k), 'after': restored['options'].get(k)} for k in ('observed_from', 'observed_to', 'selection_policy')}

    bundle['events'][0]['event']['individual_id'] = 'invented-individual'
    bundle['session']['content_hash'] = content_hash(bundle['events'])
    bundle.pop('bundle_sha256')
    bundle['bundle_sha256'] = hashlib.sha256(canonical(bundle).encode()).hexdigest()
    path.write_text(canonical(bundle))
    imported = restored_sessions.import_bundle(path)
    native = subprocess.run([BINARY, 'replay', '--db', str(root / 'restored.db'), '--session', imported['id']], text=True, capture_output=True)
    results['A4_semantics'] = {'python_replay_count': len(Timeline(restored_sessions, imported['id']).items), 'native_exit': native.returncode, 'native_error': native.stderr.strip()}

    with store.connect() as db:
        row = json.loads(db.execute('SELECT event_json FROM session_events WHERE session_id=?', (saved['id'],)).fetchone()[0])
        row['latitude'] = -42
        db.execute('UPDATE session_events SET event_json=? WHERE session_id=?', (canonical(row), saved['id']))
    try:
        Timeline(sessions, saved['id'])
        python_result = 'accepted'
    except ValueError as error:
        python_result = str(error)
    native = subprocess.run([BINARY, 'replay', '--db', str(root / 'original.db'), '--session', saved['id']], text=True, capture_output=True)
    results['A2_content_integrity'] = {'python': python_result, 'native_exit': native.returncode, 'native_changed_latitude': json.loads(native.stdout)['events'][0]['event']['latitude'] if native.returncode == 0 else None}

    with store.connect() as db:
        row['latitude'] = -41
        db.execute('UPDATE session_events SET event_json=? WHERE session_id=?', (canonical(row), saved['id']))
        options = json.loads(db.execute('SELECT options_json FROM sessions WHERE id=?', (saved['id'],)).fetchone()[0])
        options['timeline_bounds']['observed'] = ['2026-01-01T00:00:00Z', '2026-01-01T04:00:00Z']
        db.execute('UPDATE sessions SET options_json=? WHERE id=?', (canonical(options), saved['id']))
    results['A5_metadata_integrity'] = {'accepted_due_us_after_metadata_change': Timeline(sessions, saved['id']).items[0]['due_us']}
print(json.dumps(results, indent=2))
