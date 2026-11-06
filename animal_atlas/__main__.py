import argparse
import fcntl
import json
import os
from pathlib import Path
import secrets
import signal
import sys
import threading
import time
from .api import Server
from .client import Client
from .collector import Collector, load_config
from .store import Store
from .model import canonical, stamp
from .sessions import Sessions
from .replay import Timeline


def write_private(path, text):
    tmp = path.with_name(path.name + ".tmp")
    fd = os.open(str(tmp), os.O_CREAT | os.O_TRUNC | os.O_WRONLY, 0o600)
    with os.fdopen(fd, "w") as out:
        out.write(text)
        out.flush()
        os.fsync(out.fileno())
    os.replace(str(tmp), str(path))


def main():
    parser = argparse.ArgumentParser(description="Animal Atlas: local zero-service-cost collection")
    parser.add_argument("--state-dir", type=Path, default=Path(".state"))
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("serve", "collect-once"):
        cmd = sub.add_parser(command)
        cmd.add_argument("--config", type=Path)
        if command == "serve":
            cmd.add_argument("--port", type=int, default=0)
            cmd.add_argument("--duration", type=float, help="Stop after this many seconds")
            cmd.add_argument("--no-collect", action="store_true", help="Serve previously collected data only")
            cmd.add_argument("--record-seconds", type=int, help="Start an automatic recording session")
            cmd.add_argument("--session-name", default="Animal Atlas recording")
    sub.add_parser("status")
    events = sub.add_parser("events")
    events.add_argument("--limit", type=int, default=20)
    watch = sub.add_parser("watch")
    watch.add_argument("--after-seq", type=int, help="Default: subscribe from current high water")
    watch.add_argument("--include-baseline", action="store_true")
    watch.add_argument("--batches", type=int, default=0, help="Exit after this many responses; 0 = continuous")
    sub.add_parser("sessions")
    start = sub.add_parser("record-start")
    start.add_argument("--name", default="Animal Atlas recording")
    start.add_argument("--seconds", type=int)
    stop_recording = sub.add_parser("record-stop")
    stop_recording.add_argument("session_id")
    snapshot = sub.add_parser("snapshot")
    snapshot.add_argument("--name", default="Saved animal events")
    snapshot.add_argument("--source", action="append", dest="sources")
    snapshot.add_argument("--observed-from")
    snapshot.add_argument("--observed-to")
    export = sub.add_parser("session-export")
    export.add_argument("session_id")
    export.add_argument("path", type=Path)
    imported = sub.add_parser("session-import")
    imported.add_argument("path", type=Path)
    replay = sub.add_parser("replay")
    replay.add_argument("session_id")
    replay.add_argument("--mode", choices=("observed", "arrival"), default="observed")
    replay.add_argument("--speed", default="1")
    replay.add_argument("--limit", type=int, default=10)
    args = parser.parse_args()
    if args.command in ("record-start", "record-stop"):
        client = Client(args.state_dir)
        if args.command == "record-start":
            result = client.post("/v1/sessions/start", {"name": args.name, "duration_seconds": args.seconds})
        else:
            result = client.post("/v1/sessions/" + args.session_id + "/stop", {"stop": True})
        print(json.dumps(result, indent=2))
        return
    if args.command in ("sessions", "snapshot", "session-export", "session-import", "replay"):
        database = args.state_dir / "atlas.sqlite3"
        if not database.exists():
            raise ValueError("no collected database exists yet")
        sessions = Sessions(Store(database))
        if args.command in ("replay", "session-export") and args.session_id == "latest":
            choices = [s for s in sessions.list() if s["state"] in ("frozen", "interrupted")]
            if not choices:
                raise ValueError("no saved sessions available")
            args.session_id = choices[0]["id"]
        if args.command == "sessions":
            result = sessions.list()
        elif args.command == "snapshot":
            result = sessions.snapshot(args.name, args.sources, observed_from=args.observed_from, observed_to=args.observed_to)
        elif args.command == "session-export":
            result = sessions.export(args.session_id, args.path)
        elif args.command == "session-import":
            result = sessions.import_bundle(args.path)
        else:
            result = Timeline(sessions, args.session_id, args.mode, args.speed).page(limit=args.limit)
        print(json.dumps(result, indent=2))
        return
    if args.command in ("status", "events", "watch"):
        client = Client(args.state_dir)
        if args.command == "status":
            print(json.dumps(client.get("/v1/status"), indent=2))
        elif args.command == "events":
            print(json.dumps(client.get("/v1/events?limit=" + str(args.limit)), indent=2))
        else:
            cursor = client.get("/v1/status")["high_water_seq"] if args.after_seq is None else args.after_seq
            count = 0
            while True:
                response = client.changes(cursor)
                for change in response["changes"]:
                    if change["live_eligible"] or args.include_baseline:
                        print(canonical(change), flush=True)
                cursor = response["next_seq"]
                count += 1
                if args.batches and count >= args.batches:
                    break
        return
    config = load_config(args.config)
    if args.command == "serve":
        import math
        if not 0 <= args.port <= 65535 or (args.duration is not None and (not math.isfinite(args.duration) or args.duration <= 0)):
            parser.error("port/duration out of bounds")
    os.umask(0o077)
    args.state_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
    os.chmod(str(args.state_dir), 0o700)
    # Prevent two collectors against the same state, including collect-once vs serve.
    with (args.state_dir / "collector.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError("collector already running for this state directory") from None
        store = Store(args.state_dir / "atlas.sqlite3")
        sessions = Sessions(store)
        recovered = sessions.recover()
        if recovered:
            print(canonical({"interrupted_recordings_recovered": len(recovered)}), flush=True)
        stop = threading.Event()
        signal.signal(signal.SIGINT, lambda *_: stop.set())
        signal.signal(signal.SIGTERM, lambda *_: stop.set())
        collector = Collector(store, config, stop)
        for sid, entry in config.items():
            store.update_source(sid, enabled=entry["enabled"],
                collection_scope="local_research", permissions_status="unresolved", monetary_cost=0,
                configured_poll_seconds=entry["poll_seconds"])
        if args.command == "collect-once":
            failed = False
            for sid in collector.adapters:
                result, retry = collector.poll(sid)
                failed = failed or "error" in result
                print(canonical({"source": sid, **result}))
            print(json.dumps(store.status(), indent=2))
            if failed:
                sys.exit(1)
            return
        if args.record_seconds is not None:
            recording = sessions.start(args.session_name, duration_seconds=args.record_seconds)
            print(canonical({"recording_session": recording["id"], "name": recording["name"]}), flush=True)
        token_path = args.state_dir / "api.token"
        if not token_path.exists():
            write_private(token_path, secrets.token_urlsafe(32))
        os.chmod(str(token_path), 0o600)
        server = Server(store, token_path.read_text().strip(), args.port)
        server.collection_running = bool(collector.adapters) and not args.no_collect
        connection_path = args.state_dir / "connection.json"
        write_private(connection_path, canonical({"protocol_version": 1, "port": server.server_port,
                      "store_id": store.store_id, "started_at_utc": stamp()}))
        api_thread = threading.Thread(target=server.serve_forever, name="local-api", daemon=True)
        api_thread.start()
        workers = [] if args.no_collect else collector.start()
        print(canonical({"listening": "http://127.0.0.1:" + str(server.server_port),
                         "protocol_version": 1, "token_file": str(token_path.resolve())}), flush=True)
        try:
            began = time.monotonic()
            while not stop.wait(0.25):
                sessions.finish_due()
                if args.duration is not None and time.monotonic() - began >= args.duration:
                    stop.set()
        finally:
            stop.set()
            server.stopping.set()
            server.shutdown()
            server.server_close()
            api_thread.join(timeout=2)
            for worker in workers:
                worker.join(timeout=17)
            for sid in sessions.active():
                sessions.stop(sid)
            connection_path.unlink(missing_ok=True)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
    except Exception as exc:
        # Safe, bounded error categories; provider exception text is not echoed.
        print("Animal Atlas stopped: " + type(exc).__name__ + ". Check configuration, state directory, and collector availability.", file=sys.stderr)
        sys.exit(1)
