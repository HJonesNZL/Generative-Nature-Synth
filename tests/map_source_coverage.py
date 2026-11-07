"""Protocol regression: bounded map baseline preserves quieter location sources.

Synthetic disposable libraries only; no providers, application state or owner DB.
Pass the freshly built atlas-native executable as the sole positional argument.
"""
import json
from pathlib import Path
import selectors
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from animal_atlas.model import event
from animal_atlas.store import Store


class MapSourceCoverageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="atlas-map-coverage-")
        self.path = Path(self.temp.name) / "synthetic.sqlite3"
        self.store = Store(self.path)
        # An unresolved provider subject and a genuine individual/deployment are
        # independent meanings; both must survive an acoustically busy restart.
        self.shark = event(
            "synthetic_shark", "old-fix", "2026-01-01T00:00:00Z",
            "satellite_fix", "individual_fix", -41, 174,
            provider_subject_id="synthetic_shark:subject:fixture",
            quality_flags=["identity_reconciliation_pending", "position_accuracy_unknown"],
        )
        self.store.ingest("synthetic_shark", [(self.shark, {})], True)
        self.shark["longitude"] = 175
        self.store.ingest("synthetic_shark", [(self.shark, {})], True)
        self.seal = event(
            "synthetic_seal", "old-fix", "2026-01-01T00:01:00Z",
            "gps_fix", "individual_fix", 40, -70,
            individual_id="synthetic_seal:animal:fixture",
            deployment_id="synthetic_seal:deployment:fixture",
        )
        self.store.ingest("synthetic_seal", [(self.seal, {})], True)
        self.birds = [
            (event("synthetic_bird", str(index), "2026-01-01T01:00:00Z",
                   "acoustic_species_detection", "station_detection", -42, 173,
                   station_id="synthetic_bird:station:fixture"), {})
            for index in range(1200)
        ]
        self.store.ingest("synthetic_bird", self.birds, True)
        self.start_host()

    def start_host(self):
        self.process = subprocess.Popen(
            [str(BINARY), "ui-host", "--db", str(self.path)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True,
        )
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.counter = 0

    def stop_host(self):
        self.process.stdin.close()
        self.process.wait(timeout=5)
        self.selector.close()
        self.process.stdout.close()
        self.process.stderr.close()
        self.assertEqual(self.process.returncode, 0)

    def tearDown(self):
        self.stop_host()
        self.temp.cleanup()

    def request(self, store="", after="0", limit=200):
        self.counter += 1
        payload = dict(protocolVersion=1, requestId=str(self.counter), method="getChanges",
                       args=dict(storeId=store, afterSequence=after, limit=limit))
        self.process.stdin.write(json.dumps(payload) + "\n")
        self.process.stdin.flush()
        self.assertTrue(self.selector.select(timeout=5), "native protocol timed out")
        response = json.loads(self.process.stdout.readline())
        self.assertTrue(response["ok"], response)
        self.assertEqual(response["requestId"], str(self.counter))
        return response["result"]

    def assert_baseline(self, page, limit):
        self.assertLessEqual(len(page["events"]), limit)
        sequences = [int(item["event"]["revisionSequence"]) for item in page["events"]]
        self.assertEqual(sequences, sorted(set(sequences)))
        self.assertEqual(page["nextSequence"], str(self.store.high_water()))
        self.assertTrue(all(item["change"] == "baseline" and not item["liveEligible"]
                            for item in page["events"]))

    def test_old_current_fixes_survive_busy_baseline_and_restart(self):
        for restart in (False, True):
            with self.subTest(restart=restart):
                if restart:
                    self.stop_host()
                    self.start_host()
                page = self.request()
                self.assert_baseline(page, 200)
                self.assertEqual(len(page["events"]), 200)
                sources = {item["event"]["sourceId"] for item in page["events"]}
                self.assertEqual(sources, {"synthetic_bird", "synthetic_shark", "synthetic_seal"})
                shark = next(item["event"] for item in page["events"]
                             if item["event"]["sourceId"] == "synthetic_shark")
                self.assertEqual(shark["position"]["longitude"], 175)
                self.assertIsNone(shark["individualId"])
                self.assertIsNone(shark["deploymentId"])
                self.assertIn("identity_reconciliation_pending", shark["qualityFlags"])
                self.assertEqual(shark["revisionSequence"], "2")
                self.assertNotIn("1", [item["event"]["revisionSequence"] for item in page["events"]])
                seal = next(item["event"] for item in page["events"]
                            if item["event"]["sourceId"] == "synthetic_seal")
                self.assertEqual(seal["individualId"], "synthetic_seal:animal:fixture")
                self.assertEqual(seal["deploymentId"], "synthetic_seal:deployment:fixture")

    def test_small_limit_reserves_at_most_half_and_single_slot_is_latest(self):
        for limit in (1, 2, 3, 5, 20, 200, 500):
            with self.subTest(limit=limit):
                page = self.request(limit=limit)
                self.assert_baseline(page, limit)
                fixes = [item for item in page["events"] if item["event"]["locationKind"] == "individual_fix"]
                self.assertEqual(len(fixes), min(2, limit // 2))
                self.assertTrue(any(item["event"]["sourceId"] == "synthetic_bird" for item in page["events"]))
                self.assertEqual(page["events"][-1]["event"]["revisionSequence"], str(self.store.high_water()))

    def test_reserved_fix_count_is_capped_and_current_revisions_are_unique(self):
        extra = [(event("synthetic_seal", "fix-" + str(index), "2026-01-01T00:02:00Z",
                        "gps_fix", "individual_fix", 40, -70,
                        individual_id="synthetic_seal:animal:" + str(index)), {})
                 for index in range(125)]
        self.store.ingest("synthetic_seal", extra, True)
        # A newer busy batch must not consume the reserved position coverage.
        self.store.ingest("synthetic_bird", [
            (event("synthetic_bird", "later-" + str(index), "2026-01-01T02:00:00Z",
                   "acoustic_species_detection", "station_detection", -42, 173), {})
            for index in range(600)
        ], True)
        for limit in (20, 500):
            with self.subTest(limit=limit):
                page = self.request(limit=limit)
                self.assert_baseline(page, limit)
                fixes = [item for item in page["events"] if item["event"]["locationKind"] == "individual_fix"]
                self.assertEqual(len(fixes), min(100, limit // 2))
                self.assertEqual(len(page["events"]), limit)

    def test_live_cursor_continues_after_baseline_without_replaying_old_fixes(self):
        baseline = self.request()
        empty = self.request(baseline["storeId"], baseline["nextSequence"])
        self.assertEqual(empty["events"], [])
        arrival = event("synthetic_bird", "arrival", "2026-01-01T03:00:00Z",
                        "acoustic_species_detection", "station_detection", -42, 173)
        self.store.ingest("synthetic_bird", [(arrival, {})], True)
        self.shark["longitude"] = 176
        self.store.ingest("synthetic_shark", [(self.shark, {})], True)
        page = self.request(baseline["storeId"], baseline["nextSequence"], limit=1)
        self.assertEqual([item["change"] for item in page["events"]], ["new"])
        self.assertTrue(page["events"][0]["liveEligible"])
        corrected = self.request(page["storeId"], page["nextSequence"], limit=1)
        self.assertEqual([item["change"] for item in corrected["events"]], ["corrected"])
        self.assertFalse(corrected["events"][0]["liveEligible"])
        self.assertEqual(corrected["events"][0]["event"]["position"]["longitude"], 176)
        self.assertEqual(corrected["nextSequence"], str(self.store.high_water()))
        for store, after in (("another-store", "0"), (baseline["storeId"], str(self.store.high_water() + 10))):
            reset = self.request(store, after)
            self.assert_baseline(reset, 200)
            self.assertIn("synthetic_shark", {item["event"]["sourceId"] for item in reset["events"]})


if __name__ == "__main__":
    unittest.main(verbosity=2)
