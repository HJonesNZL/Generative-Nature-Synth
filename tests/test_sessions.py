import hashlib
import json
import sqlite3
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from animal_atlas.model import event, canonical, stamp, utcnow
from animal_atlas.store import Store
from animal_atlas.sessions import Sessions, content_hash
from animal_atlas.replay import Timeline


def ev(identity, observed="2026-01-01T00:00:00Z", lat=-41, source="birds"):
    return event(source, identity, observed, "acoustic_species_detection", "station_detection", lat, 174)


class SessionCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.store = Store(self.root / "db")
        self.sessions = Sessions(self.store)

    def tearDown(self):
        self.tmp.cleanup()

    def ingest(self, *events):
        source = events[0]["source_id"]
        return self.store.ingest(source, [(e, {"private_raw": "excluded"}) for e in events], True)


class SessionTests(SessionCase):
    def test_snapshot_is_immutable_after_provider_correction(self):
        self.ingest(ev("a"))
        frozen = self.sessions.snapshot("Original")
        self.ingest(ev("a", lat=-42))
        self.assertEqual(self.sessions.rows(frozen["id"])[0]["event"]["latitude"], -41)
        self.assertEqual(self.sessions.get(frozen["id"])["content_hash"], frozen["content_hash"])
        newer = self.sessions.snapshot("Updated")
        self.assertEqual(self.sessions.rows(newer["id"])[0]["event"]["latitude"], -42)

    def test_recording_excludes_baseline_prestart_and_corrections(self):
        self.ingest(ev("before"))
        session = self.sessions.start("Recording")
        self.ingest(ev("new"))
        self.ingest(ev("new", lat=-42))
        # Newly introduced source is still baseline material, not a live arrival.
        self.ingest(ev("new_source", source="sharks"))
        result = self.sessions.stop(session["id"])
        self.assertEqual(result["event_count"], 1)
        row = self.sessions.rows(session["id"])[0]
        self.assertEqual(row["event"]["provider_event_id"], "new")
        self.assertEqual(row["event"]["latitude"], -41)
        self.assertEqual(self.sessions.stop(session["id"]), result)

    def test_recording_is_durable_across_restart_and_marked_interrupted(self):
        self.ingest(ev("before"))
        recording = self.sessions.start("Interrupted")
        self.ingest(ev("during"))
        restarted = Sessions(Store(self.root / "db"))
        recovered = restarted.recover()
        self.assertEqual(len(recovered), 1)
        self.assertEqual(recovered[0]["state"], "interrupted")
        self.assertEqual(recovered[0]["event_count"], 1)
        self.assertEqual(restarted.recover(), [])
        Timeline(restarted, recording["id"])

    def test_active_session_cannot_replay_or_export(self):
        session = self.sessions.start("Active")
        with self.assertRaises(ValueError):
            Timeline(self.sessions, session["id"])
        with self.assertRaises(ValueError):
            self.sessions.bundle(session["id"])

    def test_due_session_stops_once(self):
        session = self.sessions.start("Due", duration_seconds=1)
        with self.store.connect() as db:
            db.execute("UPDATE sessions SET intended_end_at='2000-01-01T00:00:00.000000Z' WHERE id=?", (session["id"],))
        self.assertEqual(len(self.sessions.finish_due()), 1)
        self.assertEqual(self.sessions.finish_due(), [])

    def test_snapshot_source_and_half_open_time_filters(self):
        self.ingest(ev("a"), ev("b", "2026-01-01T01:00:00Z"))
        self.ingest(ev("c", source="sharks"))
        result = self.sessions.snapshot("Selection", sources=["birds"], observed_from="2026-01-01T00:00:00Z", observed_to="2026-01-01T01:00:00Z")
        self.assertEqual(result["event_count"], 1)
        self.assertEqual(self.sessions.rows(result["id"])[0]["event"]["provider_event_id"], "a")
        empty = self.sessions.snapshot("Empty filter", sources=[])
        self.assertEqual(empty["event_count"], 0)

    def test_session_capacity_is_explicit_not_silent(self):
        self.ingest(ev("a"), ev("b"))
        with patch("animal_atlas.sessions.MAX_EVENTS", 1):
            result = self.sessions.snapshot("Capped")
        self.assertTrue(result["options"]["capture_truncated"])
        self.assertEqual(result["event_count"], 1)

    def test_bundle_roundtrip_preserves_both_timelines_without_source_database(self):
        self.ingest(ev("a"), ev("b", "2026-01-01T04:00:00Z"))
        original = self.sessions.snapshot("Portable", seed=123)
        path = self.root / "piece.atlas.json"
        self.sessions.export(original["id"], path)
        other_store = Store(self.root / "other-db")
        other = Sessions(other_store)
        restored = other.import_bundle(path)
        self.assertEqual(other_store.high_water(), 0)
        self.assertEqual(other_store.status()["event_count"], 0)
        for mode in ("observed", "arrival"):
            a, b = Timeline(self.sessions, original["id"], mode, "60"), Timeline(other, restored["id"], mode, "60")
            self.assertEqual(a.items, b.items)
            self.assertEqual(a.summary(), b.summary())
        self.assertNotIn("private_raw", path.read_text())
        self.assertNotIn("api.token", path.read_text())
        with self.assertRaises(FileExistsError):
            self.sessions.export(original["id"], path)

    def test_checksum_corruption_rejected(self):
        self.ingest(ev("a"))
        session = self.sessions.snapshot("Checksum")
        bundle = self.sessions.bundle(session["id"])
        bundle["events"][0]["event"]["latitude"] = 0
        path = self.root / "tampered.json"
        path.write_text(json.dumps(bundle))
        with self.assertRaises(ValueError):
            self.sessions.import_bundle(path)

    def test_unknown_secret_fields_rejected_even_with_valid_checksum(self):
        self.ingest(ev("a"))
        session = self.sessions.snapshot("Strict input")
        bundle = self.sessions.bundle(session["id"])
        bundle["events"][0]["event"]["api_key"] = "do not import"
        bundle["session"]["content_hash"] = content_hash(bundle["events"])
        bundle.pop("bundle_sha256")
        bundle["bundle_sha256"] = hashlib.sha256(canonical(bundle).encode()).hexdigest()
        path = self.root / "unknown-field.json"
        path.write_text(json.dumps(bundle))
        with self.assertRaises(ValueError):
            self.sessions.import_bundle(path)

    def test_empty_session_is_valid_and_reproducible(self):
        session = self.sessions.snapshot("Empty")
        timeline = Timeline(self.sessions, session["id"])
        self.assertEqual(timeline.items, [])
        self.assertEqual(timeline.duration_us, 0)
        self.assertEqual(timeline.block(0, 64, 48000)["events"], [])

    def test_legacy_schema_migrates_without_losing_collected_data(self):
        self.ingest(ev("a"))
        with self.store.connect() as db:
            db.execute("PRAGMA user_version=1")
            db.execute("DROP TABLE session_events")
            db.execute("DROP TABLE sessions")
        migrated = Store(self.root / "db")
        self.assertEqual(migrated.status()["event_count"], 1)
        self.assertEqual(migrated.status()["schema_version"], 2)
        self.assertEqual(Sessions(migrated).snapshot("Migrated")["event_count"], 1)


class ReplayTests(SessionCase):
    def test_late_batch_has_distinct_observed_and_arrival_order(self):
        self.ingest(ev("early_arrival", "2026-01-01T02:00:00Z"))
        self.ingest(ev("late_upload", "2026-01-01T01:00:00Z"))
        session = self.sessions.snapshot("Late batch")
        observed = Timeline(self.sessions, session["id"], "observed")
        arrival = Timeline(self.sessions, session["id"], "arrival")
        self.assertEqual(observed.items[0]["event"]["provider_event_id"], "late_upload")
        self.assertEqual(arrival.items[0]["event"]["provider_event_id"], "early_arrival")

    def test_four_hours_into_four_minutes_preserves_order(self):
        self.ingest(ev("start"), ev("end", "2026-01-01T04:00:00Z"))
        session = self.sessions.snapshot("Four hours")
        timeline = Timeline(self.sessions, session["id"], speed="60")
        self.assertEqual(timeline.times, [0, 240000000])
        self.assertEqual([x["event"]["provider_event_id"] for x in timeline.items], ["start", "end"])

    def test_event_sample_positions_invariant_under_block_partition(self):
        self.ingest(ev("a"), ev("b", "2026-01-01T00:00:00.001000Z"),
                    ev("c", "2026-01-01T00:00:00.010000Z"), ev("d", "2026-01-01T00:00:00.025000Z"))
        session = self.sessions.snapshot("Partition")
        timeline = Timeline(self.sessions, session["id"])
        for rate in (44100, 48000, 96000):
            expected = [(item["due_us"] * rate // 1000000, item["event"]["event_id"]) for item in timeline.items]
            for size in (1, 17, 64, 257, 2048):
                actual = []
                for start in range(0, rate // 10, size):
                    actual.extend((start + item["sample_offset"], item["event"]["event_id"])
                                  for item in timeline.block(start, size, rate)["events"])
                self.assertEqual(actual, expected)

    def test_simultaneous_events_stable_and_overflow_reported(self):
        self.ingest(*[ev(str(i)) for i in range(20)])
        session = self.sessions.snapshot("Burst")
        timeline = Timeline(self.sessions, session["id"], speed="10000")
        result = timeline.block(0, 64, 48000, capacity=3)
        self.assertEqual(result["omitted"], 17)
        self.assertEqual([e["original_seq"] for e in result["events"]], [1, 2, 3])
        self.assertEqual(timeline.summary()["peak_events_in_one_second"], 20)

    def test_seek_window_is_half_open_and_repeatable(self):
        self.ingest(ev("a"), ev("b", "2026-01-01T00:00:01Z"))
        session = self.sessions.snapshot("Seek")
        timeline = Timeline(self.sessions, session["id"])
        before = timeline.block(47936, 64, 48000)
        after = timeline.block(48000, 64, 48000)
        self.assertEqual(before["events"], [])
        self.assertEqual(len(after["events"]), 1)
        self.assertEqual(after, timeline.block(48000, 64, 48000))

    def test_invalid_speed_and_bounds(self):
        session = self.sessions.snapshot("Bounds")
        for speed in ("0", "-1", "NaN", "inf", "10001", "1/0"):
            with self.assertRaises(ValueError):
                Timeline(self.sessions, session["id"], speed=speed)
        with self.assertRaises(ValueError):
            Timeline(self.sessions, session["id"], mode="live")
