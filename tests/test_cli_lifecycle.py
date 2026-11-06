import contextlib
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from animal_atlas.__main__ import main
from animal_atlas.model import event
from animal_atlas.sessions import Sessions
from animal_atlas.store import Store


class CliLifecycleTests(unittest.TestCase):
    def test_collect_once_recovers_before_acquiring_new_events(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            store = Store(root / "atlas.sqlite3")
            sessions = Sessions(store)

            def ingest(identity):
                value = event("birds", identity, "2026-01-01T00:00:00Z",
                              "acoustic_species_detection", "station_detection", -41, 174)
                store.ingest("birds", [(value, {})], True)

            ingest("baseline")
            abandoned = sessions.start("Interrupted recording")
            ingest("before-crash")

            class FixtureCollector:
                adapters = {"birds": object()}

                def __init__(self, *_):
                    pass

                def poll(self, _):
                    ingest("after-crash-new-command")
                    return {}, None

            with patch("sys.argv", ["animal_atlas", "--state-dir", directory, "collect-once"]), \
                    patch("animal_atlas.__main__.load_config", return_value={}), \
                    patch("animal_atlas.__main__.Collector", FixtureCollector), \
                    patch("animal_atlas.__main__.signal.signal"), \
                    contextlib.redirect_stdout(io.StringIO()):
                main()

            self.assertEqual(sessions.get(abandoned["id"])["state"], "interrupted")
            self.assertEqual([row["event"]["provider_event_id"]
                              for row in sessions.rows(abandoned["id"])], ["before-crash"])
            self.assertEqual(sessions.recover(), [])
            self.assertEqual(store.status()["event_count"], 3)
