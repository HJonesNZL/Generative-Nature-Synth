#!/usr/bin/env python3
"""Offline characterization/acceptance fixtures, never a production collector."""
import copy
from datetime import datetime, timezone, timedelta
from email.message import Message
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
import urllib.error

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from animal_atlas.collector import Collector, load_config
from animal_atlas.model import stamp
from animal_atlas.sources import BirdWeather, NZSharks, FetchError, Transport
from animal_atlas.store import Store

NOW = datetime(2026, 1, 1, 12, tzinfo=timezone.utc)
FIXTURES = json.loads(Path(__file__).with_name('fixtures.json').read_text())
COUNTEREXAMPLES = []

class ScriptedTransport:
    def __init__(self, steps, budget, stop_after=None, stop=None):
        self.steps = copy.deepcopy(steps)
        self.calls = []
        self.budget = budget
        self.stop_after, self.stop = stop_after, stop

    def json(self, url, body=None):
        if len(self.calls) >= self.budget or not self.steps:
            raise AssertionError('fixture request budget exhausted')
        self.calls.append((url, copy.deepcopy(body)))
        result = self.steps.pop(0)
        if self.stop_after == len(self.calls):
            self.stop.set()
        if isinstance(result, Exception):
            raise result
        return result


def page(records=(), more=False, cursor='next'):
    return {'data': {'detections': {'nodes': list(records), 'pageInfo': {'hasNextPage': more, 'endCursor': cursor}}}}


def bird(identity='one', observed='2026-01-01T11:59:00Z', latitude=-41):
    raw = copy.deepcopy(FIXTURES['bird'])
    raw.update(id=identity, timestamp=observed)
    raw['coords']['lat'] = latitude
    return raw


def shark_steps(subjects=None):
    return [FIXTURES['bootstrap'], FIXTURES['shark_config'], {'data': {'data': [FIXTURES['shark']] if subjects is None else subjects}}]


class ReliabilityChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='atlas-reliability-')
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)/'synthetic.sqlite3'
        self.store = Store(self.path)
        self.config = load_config()
        self.config['nzsharks']['enabled'] = False
        for target in ['animal_atlas.sources.utcnow', 'animal_atlas.model.utcnow']:
            patcher = patch(target, return_value=NOW)
            patcher.start()
            self.addCleanup(patcher.stop)
        for target in ['animal_atlas.store.stamp', 'animal_atlas.collector.stamp']:
            patcher = patch(target, side_effect=lambda value=None: stamp(value or NOW))
            patcher.start()
            self.addCleanup(patcher.stop)
        # Any accidental real transport use fails before a socket can be opened.
        patcher = patch('urllib.request.OpenerDirector.open', side_effect=AssertionError('network forbidden in reliability fixtures'))
        patcher.start()
        self.addCleanup(patcher.stop)

    def collector(self, steps, budget=10):
        collector = Collector(self.store, self.config)
        transport = ScriptedTransport(steps, budget)
        collector.adapters['birdweather'].transport = transport
        return collector, transport

    def note(self, identity, observed, desired):
        COUNTEREXAMPLES.append(dict(id=identity, observed=observed, proposed_acceptance=desired, fixed=False))

    def test_baseline_restart_dedup_and_correction_preserve_receipt(self):
        c, _ = self.collector([page([bird()])])
        self.assertEqual(c.poll('birdweather')[0]['inserted'], 1)
        reopened = Store(self.path)
        self.assertEqual(reopened.store_id, self.store.store_id)
        c = Collector(reopened, self.config)
        c.adapters['birdweather'].transport = ScriptedTransport([page([bird()]), page([bird(latitude=-42)])], 2)
        self.assertEqual(c.poll('birdweather')[0]['duplicates'], 1)
        self.assertEqual(c.poll('birdweather')[0]['corrected'], 1)
        revisions = reopened.changes()
        self.assertEqual([r['change_kind'] for r in revisions], ['baseline', 'corrected'])
        self.assertFalse(any(r['live_eligible'] for r in revisions))
        self.assertEqual(revisions[0]['event']['first_seen_at_utc'], revisions[1]['event']['first_seen_at_utc'])
        self.assertNotEqual(revisions[0]['event']['observed_at_utc'], revisions[0]['event']['first_seen_at_utc'])

    def test_page_cap_and_partial_baseline_remain_explicit(self):
        self.config['birdweather']['max_pages'] = 2
        c, transport = self.collector([page([bird('one')], True, 'a'), page([bird('two')], True, 'b')], 2)
        result, _ = c.poll('birdweather')
        self.assertFalse(result['complete'])
        self.assertEqual(len(transport.calls), 2)
        self.assertFalse(self.store.source('birdweather')['baseline_complete'])
        self.assertEqual(self.store.source('birdweather')['pages_fetched'], 2)

    def test_repeated_cursor_fails_without_committing_partial_page(self):
        c, _ = self.collector([page([bird()], True), page([bird('two')], True)], 2)
        self.assertEqual(c.poll('birdweather')[0]['error'], 'schema_or_pagination_error')
        self.assertEqual(self.store.high_water(), 0)

    def test_graphql_partial_error_is_not_empty_success(self):
        response = page([bird()])
        response['errors'] = [{'message': 'SECRET provider body must not enter status'}]
        c, _ = self.collector([response])
        self.assertEqual(c.poll('birdweather')[0]['error'], 'graphql_error')
        self.assertNotIn('SECRET', json.dumps(self.store.status()))
        self.assertEqual(self.store.high_water(), 0)

    def test_outage_larger_than_lookback_flags_unrecoverable_gap(self):
        adapter = BirdWeather(self.config['birdweather'], ScriptedTransport([page()], 1))
        batch = adapter.fetch({'last_complete_at_utc': stamp(NOW-timedelta(hours=2))}, threading.Event())
        self.assertTrue(batch.metadata['possible_unrecoverable_gap'])
        self.assertIn('late uploads may be missed', batch.metadata['completeness'])

    def test_retry_429_bounds_and_success_counterexample(self):
        c, _ = self.collector([FetchError('http_429', 120), page()])
        self.assertEqual(c.poll('birdweather')[1], 120)
        self.assertEqual(c.poll('birdweather')[1], 0)
        source = self.store.source('birdweather')
        self.assertEqual(source['consecutive_errors'], 0)
        self.assertIsNone(source['last_error'])
        self.assertEqual(source['retry_delay_seconds'], 120)
        self.note('stale_retry_after_success', {'healthy': True, 'retry_delay_seconds': 120}, 'Clear retry delay/deadline on a successful poll; do not present stale retry as active health.')

    def test_transport_retry_after_http_date_and_maximum(self):
        for value, expected in [('120', 120), ('Thu, 01 Jan 2026 12:03:00 GMT', 180), ('9999999', 86400), ('nan', 0)]:
            headers = Message(); headers['Retry-After'] = value
            error = urllib.error.HTTPError('https://fixture.invalid', 429, 'not logged', headers, None)
            class Opener:
                def open(self, *args, **kwargs):
                    raise error
            transport = Transport(); transport.opener = Opener()
            with self.assertRaises(FetchError) as caught:
                transport.json('https://fixture.invalid')
            self.assertEqual(caught.exception.retry_after, expected)
            self.assertEqual(str(caught.exception), 'http_429')

    def test_worker_jitter_is_bounded_and_wait_is_interruptible(self):
        class Stop:
            ended = False
            waits = []
            def is_set(self): return self.ended
            def wait(self, seconds): self.waits.append(seconds); self.ended = True
        stop = Stop()
        c = Collector(self.store, self.config, stop)
        c.poll = lambda sid: ({'error': 'synthetic'}, 120)
        with patch('animal_atlas.collector.random.uniform', return_value=1.5), patch('sys.stdout', new=io.StringIO()):
            c._worker('birdweather')
        self.assertEqual(stop.waits, [121.5])

    def test_bird_pre_cancel_makes_no_request(self):
        stop = threading.Event(); stop.set()
        transport = ScriptedTransport([], 0)
        with self.assertRaisesRegex(FetchError, 'cancelled'):
            BirdWeather(self.config['birdweather'], transport).fetch({}, stop)
        self.assertEqual(transport.calls, [])

    def test_shark_pre_cancel_counterexample(self):
        stop = threading.Event(); stop.set()
        transport = ScriptedTransport(shark_steps(), 3)
        with self.assertRaisesRegex(FetchError, 'cancelled'):
            NZSharks({}, transport).fetch({}, stop)
        self.assertEqual(len(transport.calls), 2)
        self.note('shark_cancel_before_fetch', {'cancelled_before_call': True, 'requests_made': 2}, 'Check cancellation before bootstrap and before every subsequent request.')

    def test_bird_cancel_during_final_response_counterexample(self):
        stop = threading.Event()
        transport = ScriptedTransport([page([bird()])], 1, stop_after=1, stop=stop)
        batch = BirdWeather(self.config['birdweather'], transport).fetch({}, stop)
        self.assertTrue(stop.is_set())
        self.assertTrue(batch.complete)
        self.assertEqual(len(batch.records), 1)
        self.note('cancel_after_final_response', {'cancelled': True, 'complete_batch_returned': True}, 'Cancellation should be checked after response and before publication; do not claim immediate in-flight socket cancellation.')

    def test_invalid_records_do_not_mean_confirmed_coverage(self):
        c, _ = self.collector([page([{'id': 'invalid'}])])
        result, _ = c.poll('birdweather')
        source = self.store.source('birdweather')
        self.assertTrue(result['complete'])
        self.assertTrue(source['baseline_complete'])
        self.assertEqual(source['invalid_records'], 1)
        self.assertEqual(self.store.high_water(), 0)
        self.note('all_invalid_page_exhausted', {'pagination_exhausted': True, 'invalid_records': 1, 'baseline_complete': True}, 'Keep pagination exhaustion separate from valid-data health/coverage; an all-invalid page is degraded, never ecological silence.')

    def test_successful_fetch_is_not_observation_freshness(self):
        c, _ = self.collector([page([bird(observed='2025-01-01T00:00:00Z')])])
        c.poll('birdweather')
        source = self.store.status()['sources']['birdweather']
        self.assertEqual(source['last_success_at_utc'], stamp(NOW))
        self.assertEqual(source['latest_observation_at_utc'], '2025-01-01T00:00:00.000000Z')
        self.assertEqual(source['publication_lag_seconds'], None)
        # A server returning an out-of-window record is retained, not historical coverage proof.
        self.note('outside_window_record', {'fetch_now': stamp(NOW), 'observed': source['latest_observation_at_utc']}, 'Validate/document out-of-window policy; never infer freshness from last_success or invent publication lag.')

    def test_missing_shark_is_not_deletion_and_timestamp_correction_revises(self):
        stop = threading.Event()
        first = NZSharks({}, ScriptedTransport(shark_steps(), 3)).fetch({}, stop)
        self.store.ingest('nzsharks', first.records, first.complete, first.metadata)
        changed = copy.deepcopy(FIXTURES['shark']); changed['last_position']['geometry']['coordinates'][0] = 178
        correction = NZSharks({}, ScriptedTransport(shark_steps([changed]), 3)).fetch({}, stop)
        self.store.ingest('nzsharks', correction.records, True, correction.metadata)
        missing = NZSharks({}, ScriptedTransport(shark_steps([]), 3)).fetch({}, stop)
        self.store.ingest('nzsharks', missing.records, True, missing.metadata)
        revisions = self.store.changes()
        self.assertEqual([r['change_kind'] for r in revisions], ['baseline', 'corrected'])
        self.assertEqual(len(self.store.events()), 1)
        self.assertIsNone(revisions[-1]['event']['individual_id'])
        self.assertIn('snapshots_only', first.metadata['completeness'])
        self.note('no_upstream_delete_protocol', {'source_snapshot_empty': True, 'retained_events': 1}, 'Absence from latest snapshot is not deletion; define explicit withdrawals and stale/hidden statuses without inventing tombstones.')

    def test_untrusted_shark_installation_fails_closed(self):
        transport = ScriptedTransport([{'configUrl': 'https://private.invalid/token'}], 1)
        with self.assertRaisesRegex(FetchError, 'public_config_changed_review_required'):
            NZSharks({}, transport).fetch({}, threading.Event())
        self.assertEqual(len(transport.calls), 1)


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ReliabilityChecks)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sources = ['animal_atlas/collector.py', 'animal_atlas/sources.py', 'animal_atlas/store.py', 'animal_atlas/model.py']
    report = dict(role='offline characterization plus proposed acceptance gates; counterexamples remain unfixed',
        clock_utc=stamp(NOW), tests=result.testsRun, failures=len(result.failures), errors=len(result.errors),
        network='forbidden; scripted transports only', database='fresh TemporaryDirectory per test; removed after test',
        source_sha256={p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},
        counterexamples=COUNTEREXAMPLES)
    Path(__file__).with_name('results.json').write_text(json.dumps(report, indent=2)+'\n')
    sys.exit(not result.wasSuccessful())
