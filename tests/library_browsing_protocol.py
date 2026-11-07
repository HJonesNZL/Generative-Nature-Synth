"""Disposable large-library protocol regression and optional historical benchmark.

Run: python3 -B tests/library_browsing_protocol.py build/standalone/atlas-native
Add --baseline /path/to/old/atlas-native for fresh-host timing/RSS comparison.
No providers, owner databases, audio devices, or hard timing pass thresholds.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import selectors
import statistics
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timedelta, timezone

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from animal_atlas.integrity import seal
from animal_atlas.model import canonical, event, stamp
from animal_atlas.sessions import Sessions
from animal_atlas.store import Store

FIXTURE_NAME = 'LargeLibrary-20000-events-3003-pieces-v1'
EVENT_COUNT = 20000
FILLER_COUNT = 3001
SOURCE = 'library-benchmark'


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def checksum(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


class Host:
    def __init__(self, binary, database, measure_memory=False):
        command = [str(binary), 'ui-host', '--db', str(database)]
        self.memory_measured = measure_memory and sys.platform == 'darwin'
        if self.memory_measured:
            command = ['/usr/bin/time', '-l', *command]
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        text=True, encoding='utf-8')
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.counter = 0
        self.peak_rss_bytes = None

    def __enter__(self):
        return self

    def __exit__(self, error_type, error, traceback):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)
            raise AssertionError('Native host did not retire after EOF')
        finally:
            self.selector.close()
            self.process.stdout.close()
            diagnostics = self.process.stderr.read()
            self.process.stderr.close()
        if self.memory_measured:
            match = re.search(r'(\d+)\s+maximum resident set size', diagnostics)
            require(match is not None, 'macOS time did not report native-process peak RSS')
            self.peak_rss_bytes = int(match.group(1))
        if error_type is None:
            require(self.process.returncode == 0,
                    'Native host failed: ' + diagnostics[-2000:])

    def request(self, method, arguments=None, error=None):
        self.counter += 1
        identity = 'library-protocol-' + str(self.counter)
        message = dict(protocolVersion=1, requestId=identity,
                       method=method, args=arguments or {})
        self.process.stdin.write(json.dumps(message) + '\n')
        self.process.stdin.flush()
        require(self.selector.select(timeout=30), 'Native request timed out: ' + method)
        line = self.process.stdout.readline()
        require(line, 'Native host exited before replying: ' + method)
        reply = json.loads(line)
        require(reply['requestId'] == identity, 'Native reply changed request identity')
        if error is not None:
            require(not reply['ok'] and reply['error']['code'] == error,
                    'Unexpected native error for ' + method + ': ' + str(reply))
            return reply['error']
        require(reply['ok'], 'Native request failed: ' + str(reply))
        return reply['result']

    def timed(self, method, arguments):
        start = time.perf_counter_ns()
        result = self.request(method, arguments)
        return result, (time.perf_counter_ns() - start) / 1e6


def fixture(path):
    store = Store(path)
    start = datetime(2026, 1, 1, tzinfo=timezone.utc)
    records = []
    for index in range(EVENT_COUNT):
        individual = index % 2 == 1
        details = dict(species_id='fixture:fish' if individual else 'fixture:bird',
                       common_name='Synthetic fish' if individual else 'Synthetic bird',
                       station_id='fixture:deployment-site' if individual else 'fixture:station:' + str(index % 16),
                       provenance_mode='demo', permissions_status='synthetic_fixture_only')
        if individual:
            details.update(individual_id='fixture:individual:' + str(index % 16),
                           deployment_id='fixture:deployment:' + str(index % 16))
        normalized = event(SOURCE, 'event-%05d' % index,
                           stamp(start + timedelta(milliseconds=index * 100)),
                           'gps_fix' if individual else 'acoustic_species_detection',
                           'individual_fix' if individual else 'station_detection',
                           -41, 174, **details)
        records.append((normalized, {'synthetic_raw': 'must_not_cross_protocol'}))
    inserted = store.ingest(SOURCE, records, True)
    require(inserted['inserted'] == EVENT_COUNT, 'Fixture event insertion count differs')
    sessions = Sessions(store)
    large = sessions.snapshot('Synthetic 20,000-event oldest piece',
                              observed_from='2026-01-01T00:00:00Z',
                              observed_to='2026-01-01T01:00:00Z')
    empty = sessions.snapshot('Synthetic empty template', sources=[],
                              observed_from='2026-01-01T00:00:00Z',
                              observed_to='2026-01-01T01:00:00Z')
    with store.connect() as database:
        database.execute('BEGIN IMMEDIATE')
        database.execute('UPDATE sessions SET created_at=? WHERE id=?',
                         ('2026-01-01T00:00:00.000000Z', large['id']))
        seal(database, large['id'])
        template = dict(database.execute('SELECT * FROM sessions WHERE id=?', (empty['id'],)).fetchone())
        columns = list(template)
        insert = 'INSERT INTO sessions(' + ','.join(columns) + ') VALUES (' + ','.join('?' for _ in columns) + ')'
        for index in range(FILLER_COUNT):
            row = dict(template, id='synthetic-piece-%05d' % index,
                       name='Synthetic saved piece %05d' % index,
                       state='interrupted' if index % 3 == 0 else 'frozen')
            database.execute(insert, tuple(row[key] for key in columns))
            seal(database, row['id'])
        expected = [(row['id'], row['state']) for row in database.execute(
            'SELECT id,state FROM sessions ORDER BY created_at DESC,id ASC')]
    require(len(expected) == 3003 and expected[-1][0] == large['id'], 'Fixture oldest-piece ordering differs')
    return store, large['id'], expected


def material(session_id):
    return dict(kind='session', sessionId=session_id)


def preparation(session_id, speed='60'):
    return dict(sessionId=session_id, basis='observed', speedNumerator=speed, speedDenominator='1')


def validate_page(page, offset, length):
    require(page['total'] == EVENT_COUNT, 'Material page total changed')
    require(len(page['events']) == length, 'Material page size changed')
    for position, item in enumerate(page['events'], offset):
        require(item['id'] == SOURCE + ':event-%05d' % position, 'Material event skipped, duplicated or reordered')
        require(item['provenance'] == 'demo', 'Material provenance changed')
        require((item['individualId'] is not None) == (position % 2 == 1), 'Station/individual identity semantics changed')
    require('synthetic_raw' not in json.dumps(page), 'Raw provider fixture crossed presentation boundary')


def regression(binary, path, store, target, expected):
    initial_bytes = checksum(path)
    with Host(binary, path) as host:
        capabilities = host.request('getCapabilities')
        require('listSessionsPage' in capabilities['supportedMethods'], 'Cursor pagination capability missing')
        first = host.request('listSessionsPage', dict(limit=100))
        require(first['nextCursor'] and isinstance(first['revision'], str) and first['sourceId'], 'First page omitted continuation/revision/source')
        page, observed = first, []
        while True:
            require((page['sourceId'], page['revision']) == (first['sourceId'], first['revision']), 'Unchanged pagination changed source/revision')
            observed.extend((item['id'], item['state']) for item in page['items'])
            if page['nextCursor'] is None:
                break
            page = host.request('listSessionsPage', dict(limit=100, cursor=page['nextCursor']))
        require(observed == expected, 'Large-library paging changed order, state or reachability')
        for arguments in (dict(limit=0), dict(limit=101), dict(limit=True),
                          dict(limit=100, cursor='invalid'), dict(limit=100, cursor='x' * 2049)):
            host.request('listSessionsPage', arguments, error='invalid_request')
        with Host(binary, path) as reopened:
            new_first = reopened.request('listSessionsPage', dict(limit=100))
            require(new_first['sourceId'] != first['sourceId'], 'Reopened library reused source generation')
            reopened.request('listSessionsPage', dict(limit=100, cursor=first['nextCursor']), error='stale_generation')
            recalled = reopened.request('prepareReplay', preparation(target))
            require(recalled['session']['id'] == target and recalled['eventCount'] == EVENT_COUNT,
                    'Restart could not prepare oldest piece')
        catalog = host.request('getCatalog', dict(material=material(target)))
        require({entry['id'] for entry in catalog['species']} == {'fixture:bird', 'fixture:fish'}, 'Historical catalog lost species')
        require('fixture:deployment-site' not in {entry['id'] for entry in catalog['stations']}, 'Individual deployment site became a receiver')
        prepared = host.request('prepareReplay', preparation(target))
        require(prepared['eventCount'] == EVENT_COUNT and prepared['durationUs'] == '60000000', 'Oldest-piece preparation lost event count or saved window')
        token = None
        for offset in range(0, EVENT_COUNT, 500):
            arguments = dict(material=material(target), offset=offset, limit=500)
            if token is not None:
                arguments['revision'] = token
            event_page = host.request('getMaterialEvents', arguments)
            validate_page(event_page, offset, 500)
            if token is None:
                token = event_page['revision']
            require(event_page['revision'] == token, 'Material pages crossed revisions without a fence')
        host.request('getCatalog', dict(material=material(target)))
        host.request('getMaterialEvents', dict(material=material(target), offset=0, limit=1, revision='stale'), error='stale_generation')
        require(checksum(path) == initial_bytes, 'Read-only protocol requests changed source database bytes')
        store.update_source(SOURCE, synthetic_revision_touch=True)
        host.request('listSessionsPage', dict(limit=100, cursor=first['nextCursor']), error='stale_generation')
        host.request('getMaterialEvents', dict(material=material(target), offset=500, limit=500, revision=token), error='stale_generation')
        refreshed = host.request('getMaterialEvents', dict(material=material(target), offset=500, limit=500))
        validate_page(refreshed, 500, 500)
        require(refreshed['revision'] != token, 'External commit failed to invalidate material revision')
        prepared = host.request('prepareReplay', preparation(target))
        generation = int(prepared['generation'])
        with store.connect() as database:
            original = database.execute('SELECT event_json FROM session_events WHERE session_id=? AND ordinal=0', (target,)).fetchone()[0]
            changed = json.loads(original)
            changed['common_name'] = 'Corrupt frozen payload fixture'
            database.execute('UPDATE session_events SET event_json=? WHERE session_id=? AND ordinal=0', (canonical(changed), target))
        for method, arguments in (
                ('getMaterialEvents', dict(material=material(target), offset=0, limit=1)),
                ('getCatalog', dict(material=material(target))), ('prepareReplay', preparation(target))):
            host.request(method, arguments, error='unavailable')
        with store.connect() as database:
            database.execute('UPDATE session_events SET event_json=? WHERE session_id=? AND ordinal=0', (original, target))
        recovered = host.request('prepareReplay', preparation(target))
        require(int(recovered['generation']) == generation + 1, 'Failed integrity checks changed preparation generation')
        validate_page(host.request('getMaterialEvents', dict(material=material(target), offset=0, limit=1)), 0, 1)
    return dict(pieces_reached=len(observed), events_checked=EVENT_COUNT,
                restart_and_source_cursor_rejection=True, malformed_cursors_rejected=True,
                revision_fenced_pages=True, external_commit_invalidates_cache=True,
                corrupt_warm_payload_rejected=True, failed_preparation_generation_preserved=True,
                read_only_database_bytes_unchanged_before_controlled_mutation=True)


def benchmark(binary, path, target, repeats):
    runs = []
    for _ in range(repeats):
        with Host(binary, path, measure_memory=True) as host:
            _, catalog_ms = host.timed('getCatalog', dict(material=material(target)))
            prepared, prepare_ms = host.timed('prepareReplay', preparation(target))
            require(prepared['eventCount'] == EVENT_COUNT, 'Benchmark prepared wrong material')
            warm = []
            for speed in ('30', '60', '30'):
                prepared, elapsed = host.timed('prepareReplay', preparation(target, speed))
                require(prepared['durationUs'] == str(3600000000 // int(speed)), 'Warm preparation reused wrong speed/window')
                warm.append(elapsed)
            page, first_page_ms = host.timed('getMaterialEvents', dict(material=material(target), offset=0, limit=500))
            validate_page(page, 0, 500)
            pages = []
            for offset in (500, 10000, 19500):
                next_page, elapsed = host.timed('getMaterialEvents', dict(material=material(target), offset=offset, limit=500))
                validate_page(next_page, offset, 500)
                pages.append(elapsed)
        runs.append(dict(fresh_host_catalog_ms=catalog_ms, prepare_after_catalog_ms=prepare_ms,
                         fresh_host_catalog_to_prepare_ms=catalog_ms + prepare_ms,
                         warm_prepare_ms=warm, first_500_event_page_after_prepare_ms=first_page_ms,
                         warm_500_event_page_ms=pages, peak_rss_bytes=host.peak_rss_bytes))
    medians = {key: statistics.median(run[key] for run in runs)
               for key in ('fresh_host_catalog_ms', 'prepare_after_catalog_ms',
                           'fresh_host_catalog_to_prepare_ms', 'first_500_event_page_after_prepare_ms')}
    medians['warm_prepare_ms'] = statistics.median(value for run in runs for value in run['warm_prepare_ms'])
    medians['warm_500_event_page_ms'] = statistics.median(value for run in runs for value in run['warm_500_event_page_ms'])
    rss = [run['peak_rss_bytes'] for run in runs if run['peak_rss_bytes'] is not None]
    medians['peak_rss_bytes'] = statistics.median(rss) if rss else None
    return dict(runs=runs, medians=medians)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--evidence', type=Path, default=Path('/tmp/atlas-library-browsing-protocol.json'))
    args = parser.parse_args()
    require(1 <= args.repeats <= 10, 'Benchmark repeats must be 1..10')
    binary = args.binary.resolve()
    baseline = args.baseline.resolve() if args.baseline else None
    require(binary.is_file() and (baseline is None or baseline.is_file()), 'Requested native binary is missing')
    evidence = args.evidence.resolve()
    allowed = (Path('/tmp').resolve(), Path(tempfile.gettempdir()).resolve(),
               (Path(__file__).resolve().parents[1] / 'build').resolve())
    require(any(evidence.is_relative_to(directory) for directory in allowed), 'Evidence must remain in temporary or generated build storage')
    identities = dict(current=dict(path=str(binary), sha256=checksum(binary)))
    if baseline:
        identities['baseline'] = dict(path=str(baseline), sha256=checksum(baseline))
    with tempfile.TemporaryDirectory(prefix='atlas-library-protocol-') as temporary:
        path = Path(temporary) / 'synthetic-library.sqlite3'
        store, target, expected = fixture(path)
        fixture_bytes = path.stat().st_size
        measured = {}
        if baseline:
            measured['baseline'] = benchmark(baseline, path, target, args.repeats)
            measured['current'] = benchmark(binary, path, target, args.repeats)
        checks = regression(binary, path, store, target, expected)
        for label, identity in identities.items():
            require(checksum(Path(identity['path'])) == identity['sha256'], label + ' binary changed while regression was running')
    result = dict(test='library_browsing_protocol', checked_at_utc=stamp(), fixture=FIXTURE_NAME,
                  fixture_event_count=EVENT_COUNT, fixture_saved_piece_count=len(expected),
                  fixture_database_bytes=fixture_bytes, binaries=identities, checks=checks,
                  benchmark=measured, network_requests=0, owner_database_access=False,
                  audio_device_access=False,
                  limitations=['Fresh host means a new process; OS file caches were not flushed.',
                               'Timings include JSON protocol serialization/parsing and exclude process startup.',
                               'Peak RSS is whole benchmark child-process maximum, measured only on macOS.',
                               'No timing thresholds or playback/listening qualification are inferred.'])
    if baseline:
        before, after = measured['baseline']['medians'], measured['current']['medians']
        result['baseline_over_current_ratio'] = {
            key: before[key] / after[key] for key in before
            if key.endswith('_ms') and after[key] > 0}
    evidence.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(evidence=str(evidence), checks=checks,
                          benchmark_medians={key: value['medians'] for key, value in measured.items()}), indent=2))


if __name__ == '__main__':
    main()
