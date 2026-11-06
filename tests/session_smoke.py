"""Explicit live recording -> freeze -> offline replay/portable restore check."""
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from animal_atlas.client import Client
from animal_atlas.sessions import Sessions
from animal_atlas.store import Store
from animal_atlas.replay import Timeline
from animal_atlas.model import stamp


def main():
    state = ROOT / ".state"
    state.mkdir(exist_ok=True)
    with (state / "session-smoke.log").open("w") as log:
        process = subprocess.Popen([sys.executable, "-B", "-m", "animal_atlas", "--state-dir", str(state),
            "serve", "--config", str(ROOT / "config/local.json"), "--record-seconds", "80",
            "--session-name", "NZ live arrivals — milestone recording", "--duration", "90"],
            cwd=str(ROOT), stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 10
            while not (state / "connection.json").exists():
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("backend failed to start")
                time.sleep(0.05)
            client = Client(state)
            active = [s for s in client.get("/v1/sessions")["sessions"] if s["state"] == "recording"]
            assert len(active) == 1
            sid = active[0]["id"]
            # Observe auto-finalisation through the same interface the software will use.
            while process.poll() is None:
                time.sleep(0.5)
            assert process.returncode == 0
            sessions = Sessions(Store(state / "atlas.sqlite3"))
            recorded = sessions.get(sid)
            assert recorded["state"] == "frozen" and recorded["event_count"] > 0
            # Offline path: collector has exited; no network clients are used below.
            snapshot = sessions.snapshot("NZ birds — saved real activity", sources=["birdweather"], seed=42)
            folder = ROOT / "Sessions"
            folder.mkdir(mode=0o700, exist_ok=True)
            path = folder / (snapshot["id"] + ".atlas.json")
            sessions.export(snapshot["id"], path)
            observed = Timeline(sessions, snapshot["id"], "observed", "60")
            arrival = Timeline(sessions, snapshot["id"], "arrival", "60")
            with tempfile.TemporaryDirectory() as tmp:
                portable = Sessions(Store(Path(tmp) / "offline.sqlite3"))
                restored = portable.import_bundle(path)
                for mode in ("observed", "arrival"):
                    a = Timeline(sessions, snapshot["id"], mode, "60")
                    b = Timeline(portable, restored["id"], mode, "60")
                    assert a.items == b.items and a.summary() == b.summary()
                assert portable.store.high_water() == 0
            evidence = dict(tested_at_utc=stamp(), live_recording=recorded, cached_snapshot=snapshot,
                observed_replay_60x=observed.summary(), arrival_replay_60x=arrival.summary(),
                bundle_path=str(path.relative_to(ROOT)), offline_roundtrip_verified=True,
                imported_session_created_no_live_events=True, collector_stopped=True,
                limitations=["data replay only; no synthesis", "short test; not a soak", "local archive; redistribution rights unresolved"])
            (ROOT / "evidence/session-milestone.json").write_text(json.dumps(evidence, indent=2) + "\n")
            print(json.dumps({"recorded_events": recorded["event_count"], "snapshot_events": snapshot["event_count"],
                "offline_roundtrip_verified": True, "replay_60x_duration_seconds": observed.duration_us / 1000000}, indent=2))
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
