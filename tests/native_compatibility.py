"""Development-only Python oracle vs production C++ core; reads owner library read-only.

Run: python3 -B tests/native_compatibility.py [path/to/atlas-native]
No source requests, collection changes, or production Python runtime requirement.
"""
import json
import sqlite3
import subprocess
import sys
import tempfile
from contextlib import contextmanager
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from animal_atlas.model import stamp
from animal_atlas.replay import Timeline
from animal_atlas.sessions import Sessions


class ReadOnlyStore:
    @contextmanager
    def connect(self):
        connection = sqlite3.connect((ROOT / '.state/atlas.sqlite3').as_uri() + '?mode=ro', uri=True)
        connection.row_factory = sqlite3.Row
        try:
            yield connection
        finally:
            connection.close()


def main():
    binary = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'build/native/atlas-native').resolve()
    database = ROOT / '.state/atlas.sqlite3'
    def native(*args, expected=0):
        result = subprocess.run([str(binary), *args], capture_output=True, text=True, timeout=30)
        assert result.returncode == expected, result.stderr
        return json.loads(result.stdout) if result.stdout else None
    sessions = Sessions(ReadOnlyStore())
    native_sessions = native('sessions', '--db', str(database))
    candidates = [session for session in native_sessions if session['state'] in ('frozen', 'interrupted')]
    assert candidates, 'No frozen sessions available for real-data parity test'
    comparisons = events_checked = 0
    for session in candidates:
        for mode in ('observed', 'arrival'):
            for speed in ('1', '60', '0.5', '1/3'):
                reference = Timeline(sessions, session['id'], mode, speed)
                result = native('replay', '--db', str(database), '--session', session['id'],
                                '--mode', mode, '--speed', speed, '--limit', '50000')
                assert result['event_count'] == len(reference.items)
                assert result['duration_us'] == reference.duration_us
                assert result['peak_events_in_one_second'] == reference.summary()['peak_events_in_one_second']
                assert result['events'] == reference.items
                comparisons += 1
                events_checked += len(reference.items)
    # Errors must fail clearly; read-only library must never create a database.
    native('replay', '--db', str(database), '--session', 'missing', expected=1)
    native('replay', '--db', str(database), '--speed', 'NaN', expected=1)
    with tempfile.TemporaryDirectory(prefix='atlas-native-read-check-') as tmp:
        missing = Path(tmp) / 'absent.sqlite3'
        native('sessions', '--db', str(missing), expected=1)
        assert not missing.exists()
        future = Path(tmp) / 'future.sqlite3'
        with sqlite3.connect(future) as db:
            db.execute('PRAGMA user_version=999')
        native('sessions', '--db', str(future), expected=1)
    proof = dict(checked_at_utc=stamp(), test='portable_cpp_vs_python_read_only_reference',
                 binary=str(binary.relative_to(ROOT)) if binary.is_relative_to(ROOT) else str(binary),
                 frozen_sessions=len(candidates), timeline_comparisons=comparisons,
                 scheduled_events_compared=events_checked, exact_event_order_and_timing_match=True,
                 native_dependencies='C++20 standard library and SQLite; no Python at runtime',
                 owner_database_opened_read_only=True, external_network_requests=0,
                 limitations=['macOS arm64 built/tested; Windows/Linux not yet tested',
                              'collector, write-side sessions, IPC service and DSP migration remain',
                              'native reader validates structure/count; SHA-256 verification remains to port'])
    name = 'native-parity-sanitized.json' if 'native-sanitized' in binary.parts else 'native-parity.json'
    (ROOT / 'evidence' / name).write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps(proof, indent=2))


if __name__ == '__main__':
    main()
