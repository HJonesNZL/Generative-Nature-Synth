import json
import tempfile
import threading
import unittest
import urllib.request
import urllib.error
from pathlib import Path
from datetime import timedelta
from animal_atlas.model import event, stamp, utcnow, parse_time
from animal_atlas.store import Store
from animal_atlas.sources import BirdWeather, NZSharks, FetchError, Batch
from animal_atlas.collector import Collector, load_config
from animal_atlas.api import Server
from animal_atlas.client import Client


def sample(identity="one", lat=-41):
    return event("test", identity, "2026-01-01T00:00:00Z", "acoustic_species_detection", "station_detection", lat, 174)


def bird(identity="b1"):
    return dict(id=identity, timestamp="2026-01-01T13:00:00+13:00", coords={"lat": -41, "lon": 174},
                species={"id": "s", "commonName": "Test bird", "scientificName": "Testus birdus"},
                station={"id": "st"}, confidence=0.5)


class FakeTransport:
    def __init__(self, responses):
        self.responses = iter(responses)
        self.calls = []

    def json(self, url, body=None):
        self.calls.append((url, body))
        result = next(self.responses)
        if isinstance(result, Exception):
            raise result
        return result


def page(nodes, more=False, cursor="next"):
    return {"data": {"detections": {"nodes": nodes, "pageInfo": {"hasNextPage": more, "endCursor": cursor}}}}


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / "db.sqlite3"
        self.store = Store(self.path)

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, ev=None, complete=True):
        return self.store.ingest("test", [(ev or sample(), {"source_record": 1})], complete)

    def test_baseline_restart_and_repeated_poll(self):
        self.put()
        restarted = Store(self.path)
        result = restarted.ingest("test", [(sample(), {})], True)
        self.assertEqual(result["duplicates"], 1)
        self.assertEqual(restarted.high_water(), 1)
        self.assertFalse(restarted.changes()[0]["live_eligible"])

    def test_new_after_baseline_is_live(self):
        self.put()
        self.put(sample("two"))
        self.assertTrue(self.store.changes(1)[0]["live_eligible"])

    def test_partial_first_poll_does_not_end_baseline(self):
        self.put(complete=False)
        self.assertFalse(self.store.source("test")["baseline_complete"])
        self.put(sample("two"), complete=True)
        self.assertTrue(all(not x["live_eligible"] for x in self.store.changes()))

    def test_empty_success_is_baseline(self):
        self.store.ingest("test", [], True)
        self.put()
        self.assertTrue(self.store.changes()[0]["live_eligible"])

    def test_store_identity_survives_restart(self):
        self.assertEqual(self.store.store_id, Store(self.path).store_id)

    def test_corrections_and_reversions_preserve_history(self):
        self.put()
        self.put(sample(lat=-42))
        self.put(sample())
        changes = self.store.changes()
        self.assertEqual([c["change_kind"] for c in changes], ["baseline", "corrected", "corrected"])
        self.assertEqual(self.store.status()["event_count"], 1)
        self.assertEqual(self.store.events()[0]["latitude"], -41)
        self.assertTrue(all(not c["live_eligible"] for c in changes))
        self.assertEqual(len({c["event"]["first_seen_at_utc"] for c in changes}), 1)

    def test_failure_rolls_back_events_and_checkpoint(self):
        with self.assertRaises(ValueError):
            self.store.ingest("test", [(sample(), {}), (dict(sample("two"), source_id="other"), {})], True)
        self.assertEqual(self.store.high_water(), 0)
        self.assertEqual(self.store.source("test"), {})

    def test_imported_event_is_never_live(self):
        self.store.ingest("test", [], True)
        self.put(dict(sample(), provenance_mode="imported"))
        self.assertFalse(self.store.changes()[0]["live_eligible"])

    def test_change_batches_resume_without_skip(self):
        self.store.ingest("test", [(sample(str(i)), {}) for i in range(503)], True)
        first = self.store.changes(0, 200)
        second = self.store.changes(first[-1]["seq"], 200)
        third = self.store.changes(second[-1]["seq"], 200)
        self.assertEqual(len(first + second + third), 503)
        self.assertEqual(len({x["seq"] for x in first + second + third}), 503)

    def test_same_timestamp_event_pagination(self):
        self.store.ingest("test", [(sample(str(i)), {}) for i in range(10)], True)
        first = self.store.events(limit=4)
        last = first[-1]
        second = self.store.events((last["observed_at_utc"], last["event_id"]), 10)
        self.assertEqual(len(first + second), 10)
        self.assertEqual(len({x["event_id"] for x in first + second}), 10)


class AdapterTests(unittest.TestCase):
    def test_bird_is_station_not_individual(self):
        ev = BirdWeather.normalize(bird())
        self.assertIsNone(ev["individual_id"])
        self.assertIsNone(ev["deployment_id"])
        self.assertEqual(ev["observed_at_utc"], "2026-01-01T00:00:00.000000Z")
        self.assertEqual(ev["station_id"], "birdweather:station:st")

    def test_coordinates_and_timezone_validation(self):
        for bad in [float("nan"), 91, True]:
            with self.assertRaises(ValueError):
                sample(lat=bad)
        with self.assertRaises(ValueError):
            event("test", "x", "2026-01-01T00:00:00", "x", "x")
        with self.assertRaises(ValueError):
            event("test", "x", stamp(utcnow() + timedelta(days=1)), "x", "x")

    def test_missing_location_preserved(self):
        ev = event("test", "x", "2026-01-01T00:00:00Z", "x", "unknown")
        self.assertIn("location_missing", ev["quality_flags"])

    def test_pagination_and_invalid_record(self):
        transport = FakeTransport([page([bird(), {"id": "broken"}], True), page([bird("b2")])])
        adapter = BirdWeather(load_config()["birdweather"], transport)
        batch = adapter.fetch({}, threading.Event())
        self.assertTrue(batch.complete)
        self.assertEqual(len(batch.records), 2)
        self.assertEqual(batch.metadata["invalid_records"], 1)
        self.assertEqual(transport.calls[1][1]["variables"]["after"], "next")

    def test_page_cap_marks_partial(self):
        cfg = dict(load_config()["birdweather"], max_pages=1)
        batch = BirdWeather(cfg, FakeTransport([page([bird()], True)])).fetch({}, threading.Event())
        self.assertFalse(batch.complete)

    def test_repeated_cursor_rejected(self):
        adapter = BirdWeather(load_config()["birdweather"], FakeTransport([page([], True), page([], True)]))
        with self.assertRaises(FetchError):
            adapter.fetch({}, threading.Event())

    def test_graphql_partial_errors_reject_whole_poll(self):
        response = dict(page([bird()]), errors=[{"message": "do not log provider text"}])
        with self.assertRaises(FetchError):
            BirdWeather(load_config()["birdweather"], FakeTransport([response])).fetch({}, threading.Event())

    def test_downtime_is_not_reported_as_complete_history(self):
        batch = BirdWeather(load_config()["birdweather"], FakeTransport([page([])])).fetch(
            {"last_complete_at_utc": "2026-01-01T00:00:00Z"}, threading.Event())
        self.assertTrue(batch.metadata["possible_unrecoverable_gap"])

    def test_shark_coordinates_identity_and_fix_id(self):
        bootstrap = {"configUrl": "https://map.sustainableoceansociety.co.nz/public/configs/config.json"}
        cfg = {"server": "ermap-server.pamdas.org", "public_name": "nzsharkman", "subject_group": "group",
               "subjects": {"s": {"animal_name": "Test shark"}}}
        subject = {"id": "s", "last_position": {"geometry": {"type": "Point", "coordinates": [179, -45]},
                                                   "properties": {"DateTime": "2026-01-01T00:00:00Z"}}}
        batch = NZSharks({}, FakeTransport([bootstrap, cfg, {"data": {"data": [subject]}}])).fetch({}, threading.Event())
        ev = batch.records[0][0]
        self.assertEqual((ev["latitude"], ev["longitude"]), (-45, 179))
        self.assertIsNone(ev["individual_id"])
        self.assertEqual(ev["provider_subject_id"], "nzsharks:subject:s")
        self.assertIn("snapshots_only", batch.metadata["completeness"])

    def test_untrusted_config_target_rejected(self):
        with self.assertRaises(FetchError):
            NZSharks({}, FakeTransport([{"configUrl": "https://private.invalid/"}])).fetch({}, threading.Event())

    def test_rate_limit_does_not_complete_baseline(self):
        with tempfile.TemporaryDirectory() as tmp:
            store = Store(Path(tmp) / "db")
            collector = Collector(store, load_config())
            collector.adapters["birdweather"].transport = FakeTransport([FetchError("http_429", 120)])
            result, delay = collector.poll("birdweather")
            self.assertEqual(delay, 120)
            self.assertEqual(result["error"], "http_429")
            self.assertFalse(store.source("birdweather").get("baseline_complete", False))

    def test_changed_scope_restarts_baseline(self):
        with tempfile.TemporaryDirectory() as tmp:
            store = Store(Path(tmp) / "db")
            config = load_config()
            config["nzsharks"]["enabled"] = False
            collector = Collector(store, config)
            collector.adapters["birdweather"].transport = FakeTransport([page([bird()])])
            collector.poll("birdweather")
            config["birdweather"]["bbox"] = None
            collector.adapters["birdweather"].transport = FakeTransport([page([bird("new-region")])])
            collector.poll("birdweather")
            self.assertFalse(store.changes()[-1]["live_eligible"])

    def test_unexpected_adapter_failure_marks_unhealthy(self):
        with tempfile.TemporaryDirectory() as tmp:
            store = Store(Path(tmp) / "db")
            collector = Collector(store, load_config())
            collector.adapters["birdweather"].transport = FakeTransport([{}])
            result, _ = collector.poll("birdweather")
            self.assertIn("error", result)
            self.assertIsNotNone(store.source("birdweather")["last_error"])

    def test_configuration_rejects_paid_endpoint_and_fast_poll(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.json"
            for cfg in [{"paid": {}}, {"birdweather": {"poll_seconds": 1}},
                        {"birdweather": {"endpoint": "https://example.invalid"}},
                        {"birdweather": {"bbox": [-48, 179, -34, -179]}}]:
                path.write_text(json.dumps(cfg))
                with self.assertRaises(ValueError):
                    load_config(path)


class APITests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.store = Store(self.root / "db")
        self.store.ingest("test", [(sample(), {})], True)
        self.server = Server(self.store, "test-token")
        (self.root / "api.token").write_text("test-token")
        (self.root / "connection.json").write_text(json.dumps({"port": self.server.server_port}))
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()
        self.client = Client(self.root)

    def tearDown(self):
        self.server.stopping.set()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.tmp.cleanup()

    def test_auth_and_origin_protection(self):
        for headers, expected in [({}, 401), ({"Authorization": "Bearer test-token", "Origin": "https://evil.invalid"}, 403)]:
            request = urllib.request.Request(self.client.base + "/v1/status", headers=headers)
            with self.assertRaises(urllib.error.HTTPError) as error:
                self.client.opener.open(request)
            self.assertEqual(error.exception.code, expected)

    def test_baseline_then_live_long_poll(self):
        response = self.client.changes(0, wait=0)
        self.assertFalse(response["changes"][0]["live_eligible"])
        timer = threading.Timer(0.1, lambda: self.store.ingest("test", [(sample("two"), {})], True))
        timer.start()
        response = self.client.changes(response["next_seq"], wait=2)
        timer.join()
        self.assertTrue(response["changes"][0]["live_eligible"])
        self.assertEqual(response["next_seq"], 2)

    def test_bounds_and_cursor_reset(self):
        for route, expected in [("/v1/changes?after_seq=999", 409), ("/v1/events?limit=501", 400),
                                ("/v1/changes?wait=999", 400), ("/v1/events?before=broken", 400)]:
            with self.assertRaises(urllib.error.HTTPError) as error:
                self.client.get(route)
            self.assertEqual(error.exception.code, expected)

    def test_store_replacement_is_explicit(self):
        with self.assertRaises(urllib.error.HTTPError) as error:
            self.client.get("/v1/changes?store_id=another-database")
        self.assertEqual(error.exception.code, 409)

    def test_api_does_not_expose_raw_payload(self):
        response = self.client.get("/v1/events")
        self.assertNotIn("source_record", json.dumps(response))
        self.assertEqual(self.client.get("/v1/status")["event_count"], 1)

    def test_session_api_record_freeze_and_replay(self):
        session = self.client.post("/v1/sessions/start", {"name": "API recording", "seed": 7})
        self.store.ingest("test", [(sample("during"), {})], True)
        result = self.client.post("/v1/sessions/" + session["id"] + "/stop", {"stop": True})
        self.assertEqual(result["event_count"], 1)
        timeline = self.client.get("/v1/sessions/" + session["id"] + "/timeline?speed=60")
        self.assertEqual(timeline["event_count"], 1)
        self.assertEqual(timeline["events"][0]["event"]["provider_event_id"], "during")
        rows = self.client.get("/v1/sessions/" + session["id"] + "/events")
        self.assertEqual(rows["next_ordinal"], 0)

    def test_session_api_rejects_extra_fields_and_missing_session(self):
        with self.assertRaises(urllib.error.HTTPError) as error:
            self.client.post("/v1/sessions/snapshot", {"name": "bad", "api_token": "not_allowed"})
        self.assertEqual(error.exception.code, 400)
        with self.assertRaises(urllib.error.HTTPError) as error:
            self.client.get("/v1/sessions/missing")
        self.assertEqual(error.exception.code, 404)


if __name__ == "__main__":
    unittest.main()
