"""Same acceptance assertions run against original and temporary patched package."""
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.append(str(ROOT))
from animal_atlas.collector import Collector, load_config
from animal_atlas.sources import BirdWeather, NZSharks, FetchError, Batch
from animal_atlas.store import Store

FIXTURES = json.loads(Path(__file__).with_name('fixtures.json').read_text())

class Fake:
    def __init__(self, responses, stop=None):
        self.responses, self.stop, self.calls = responses, stop, 0
    def json(self, url, body=None):
        self.calls += 1
        if not self.responses:
            raise AssertionError('request budget exhausted')
        if self.stop:
            self.stop.set()
        return self.responses.pop(0)


def response(nodes):
    return {'data': {'detections': {'nodes': nodes, 'pageInfo': {'hasNextPage': False, 'endCursor': None}}}}

class Acceptance(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='atlas-candidate-')
        self.addCleanup(self.temp.cleanup)
        self.store = Store(Path(self.temp.name)/'fixture.sqlite3')
        self.config = load_config()
        self.config['nzsharks']['enabled'] = False
        deny = patch('urllib.request.OpenerDirector.open', side_effect=AssertionError('network forbidden'))
        deny.start(); self.addCleanup(deny.stop)

    def test_pre_cancelled_shark_sends_zero_requests(self):
        stop = threading.Event(); stop.set()
        transport = Fake([FIXTURES['bootstrap'], FIXTURES['shark_config']])
        with self.assertRaisesRegex(FetchError, 'cancelled'):
            NZSharks({}, transport).fetch({}, stop)
        self.assertEqual(transport.calls, 0)

    def test_cancelled_final_bird_response_is_not_publishable(self):
        stop = threading.Event()
        transport = Fake([response([FIXTURES['bird']])], stop)
        with self.assertRaisesRegex(FetchError, 'cancelled'):
            BirdWeather(self.config['birdweather'], transport).fetch({}, stop)

    def test_cancelled_batch_cannot_enter_store(self):
        stop = threading.Event()
        collector = Collector(self.store, self.config, stop)
        record = BirdWeather.normalize(FIXTURES['bird'])
        class Adapter:
            def fetch(self, previous, stop):
                stop.set()
                return Batch([(record, FIXTURES['bird'])], True, {})
        collector.adapters['birdweather'] = Adapter()
        collector.poll('birdweather')
        self.assertEqual(self.store.high_water(), 0)

    def test_invalid_initial_page_keeps_baseline_until_valid_exhaustion(self):
        collector = Collector(self.store, self.config)
        collector.adapters['birdweather'].transport = Fake([response([{'id':'invalid'}]), response([FIXTURES['bird']])])
        collector.poll('birdweather')
        status = self.store.source('birdweather')
        self.assertTrue(status['pagination_exhausted'])
        self.assertEqual(status['invalid_records'], 1)
        self.assertFalse(status['baseline_complete'])
        self.assertNotIn('last_complete_at_utc', status)
        collector.poll('birdweather')
        self.assertTrue(self.store.source('birdweather')['baseline_complete'])
        self.assertEqual(self.store.changes()[0]['change_kind'], 'baseline')
        self.assertFalse(self.store.changes()[0]['live_eligible'])

if __name__ == '__main__':
    unittest.main(verbosity=2)
