"""Explicit real-network integration check; never runs in unittest discovery.

Run from project root: python3 tests/live_smoke.py
Makes bounded public-source requests and writes summary evidence, not raw feeds.
"""
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from animal_atlas.client import Client
from animal_atlas.store import Store
from animal_atlas.model import stamp


def main():
    state = ROOT / ".state"
    initial = Store(state / "atlas.sqlite3").high_water()
    with (state / "live-smoke.log").open("w") as log:
        process = subprocess.Popen([sys.executable, "-B", "-m", "animal_atlas", "--state-dir", str(state),
                                    "serve", "--config", str(ROOT / "config/local.json"), "--duration", "90"],
                                   cwd=str(ROOT), stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 10
            while not (state / "connection.json").exists():
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("API failed to start")
                time.sleep(0.05)
            client = Client(state)
            cursor, delivered, live = initial, [], []
            deadline = time.monotonic() + 80
            while time.monotonic() < deadline:
                response = client.changes(cursor, wait=10)
                changes = response["changes"]
                if changes:
                    # Independent client gets exactly the same durable batch.
                    other = Client(state).changes(cursor, wait=0)
                    assert [c["seq"] for c in changes] == [c["seq"] for c in other["changes"]]
                delivered.extend(c["seq"] for c in changes)
                live.extend(c for c in changes if c["live_eligible"])
                cursor = response["next_seq"]
            assert delivered == sorted(set(delivered)), "duplicate or out-of-order delivery"
            assert live, "no real live arrivals observed during bounded test"
            status = client.get("/v1/status")
            evidence = dict(tested_at_utc=stamp(), test="real_collect_persist_subscribe",
                            duration_seconds=90, initial_seq=initial, final_seq=cursor,
                            delivered_revision_count=len(delivered), live_arrival_count=len(live),
                            independent_consumer_match=True,
                            first_live_event_id=live[0]["event"]["event_id"],
                            last_live_observed_at_utc=max(c["event"]["observed_at_utc"] for c in live),
                            status=status,
                            limitations=["short smoke test, not a soak test", "no audio or GUI attached",
                                         "commercial source permissions unresolved", "rolling-window completeness unproven"])
            (ROOT / "evidence/live-smoke.json").write_text(json.dumps(evidence, indent=2) + "\n")
            print(json.dumps({k: v for k, v in evidence.items() if k != "status"}, indent=2))
            process.wait(timeout=25)
            assert process.returncode == 0
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
