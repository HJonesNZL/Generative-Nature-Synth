"""Atomic ingestion + durable change log. Acknowledgement belongs to each consumer."""
import json
import sqlite3
import uuid
import shutil
from contextlib import contextmanager
from pathlib import Path
from .model import canonical, digest, stamp, validate_event


class Store:
    def __init__(self, path):
        self.path = str(path)
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        with self.connect() as db:
            version = db.execute("PRAGMA user_version").fetchone()[0]
            if version not in (0, 1, 2, 3):
                raise ValueError("unsupported database schema")
            db.execute("PRAGMA journal_mode=WAL")
            db.execute("BEGIN IMMEDIATE")
            version = db.execute("PRAGMA user_version").fetchone()[0]
            if version not in (0, 1, 2, 3):
                raise ValueError("unsupported database schema")
            schema = """
                CREATE TABLE IF NOT EXISTS sources (
                    id TEXT PRIMARY KEY, status TEXT NOT NULL);
                CREATE TABLE IF NOT EXISTS metadata (key TEXT PRIMARY KEY,value TEXT NOT NULL);
                CREATE TABLE IF NOT EXISTS polls (
                    id INTEGER PRIMARY KEY AUTOINCREMENT, source_id TEXT NOT NULL,
                    completed_at TEXT NOT NULL, complete INTEGER NOT NULL, details TEXT NOT NULL);
                CREATE TABLE IF NOT EXISTS revisions (
                    seq INTEGER PRIMARY KEY AUTOINCREMENT,
                    event_id TEXT NOT NULL, source_id TEXT NOT NULL,
                    fingerprint TEXT NOT NULL, observed_at TEXT NOT NULL,
                    first_seen_at TEXT NOT NULL, ingested_at TEXT NOT NULL,
                    change_kind TEXT NOT NULL, live_eligible INTEGER NOT NULL,
                    event_json TEXT NOT NULL, raw_json TEXT NOT NULL);
                CREATE INDEX IF NOT EXISTS revisions_source ON revisions(source_id,seq);
                CREATE TABLE IF NOT EXISTS events (
                    id TEXT PRIMARY KEY, source_id TEXT NOT NULL,
                    observed_at TEXT NOT NULL, revision_seq INTEGER NOT NULL,
                    FOREIGN KEY(revision_seq) REFERENCES revisions(seq));
                CREATE INDEX IF NOT EXISTS events_time ON events(observed_at,id);
                CREATE INDEX IF NOT EXISTS events_source ON events(source_id,observed_at);
                CREATE TABLE IF NOT EXISTS sessions (
                    id TEXT PRIMARY KEY, name TEXT NOT NULL, state TEXT NOT NULL,
                    created_at TEXT NOT NULL, ended_at TEXT,
                    start_seq INTEGER NOT NULL, end_seq INTEGER,
                    intended_end_at TEXT, origin TEXT NOT NULL,
                    options_json TEXT NOT NULL, event_count INTEGER NOT NULL DEFAULT 0,
                    content_hash TEXT);
                CREATE TABLE IF NOT EXISTS session_events (
                    session_id TEXT NOT NULL REFERENCES sessions(id),
                    ordinal INTEGER NOT NULL, original_seq INTEGER NOT NULL,
                    event_json TEXT NOT NULL, PRIMARY KEY(session_id,ordinal));
            """
            for statement in schema.split(";"):
                if statement.strip():
                    db.execute(statement)
            if version < 3:
                db.execute("ALTER TABLE sessions ADD COLUMN envelope_version INTEGER NOT NULL DEFAULT 0")
                db.execute("ALTER TABLE sessions ADD COLUMN envelope_json TEXT")
                db.execute("ALTER TABLE sessions ADD COLUMN envelope_hash TEXT")
                db.execute("PRAGMA user_version=3")
            db.execute("INSERT OR IGNORE INTO metadata VALUES ('store_id',?)", (str(uuid.uuid4()),))
            self.store_id = db.execute("SELECT value FROM metadata WHERE key='store_id'").fetchone()[0]

    @contextmanager
    def connect(self):
        db = sqlite3.connect(self.path, timeout=5)
        db.row_factory = sqlite3.Row
        db.execute("PRAGMA foreign_keys=ON")
        db.execute("PRAGMA synchronous=FULL")
        try:
            with db:
                yield db
        finally:
            db.close()

    def source(self, source_id):
        with self.connect() as db:
            row = db.execute("SELECT status FROM sources WHERE id=?", (source_id,)).fetchone()
            return json.loads(row[0]) if row else {}

    def update_source(self, source_id, **fields):
        with self.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            row = db.execute("SELECT status FROM sources WHERE id=?", (source_id,)).fetchone()
            status = json.loads(row[0]) if row else {}
            status.update(fields)
            db.execute("INSERT OR REPLACE INTO sources VALUES (?,?)", (source_id, canonical(status)))

    def ingest(self, source_id, records, complete, metadata=None):
        """records = (normalized event, source record). Checkpoint commits with records.

        Baseline ends ONLY on a complete poll. Corrections are never note triggers.
        Records already persisted during interrupted baseline remain baseline records.
        """
        inserted = corrected = duplicates = 0
        if self.disk_bytes() >= 512 * 1024 * 1024:
            raise OSError("local collection storage limit reached")
        if shutil.disk_usage(Path(self.path).parent).free < 64 * 1024 * 1024:
            raise OSError("insufficient free disk space")
        now = stamp()
        with self.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            row = db.execute("SELECT status FROM sources WHERE id=?", (source_id,)).fetchone()
            status = json.loads(row[0]) if row else {}
            baseline = not status.get("baseline_complete", False)
            for normalized, raw in records:
                validate_event(normalized)
                if normalized["source_id"] != source_id:
                    raise ValueError("source mismatch")
                fingerprint = digest(normalized)
                previous = db.execute("""SELECT r.* FROM events e JOIN revisions r
                    ON r.seq=e.revision_seq WHERE e.id=?""", (normalized["event_id"],)).fetchone()
                if previous and previous["fingerprint"] == fingerprint:
                    duplicates += 1
                    continue
                kind = "corrected" if previous else "baseline" if baseline else "new"
                live = kind == "new" and normalized.get("provenance_mode") == "live_source"
                first_seen = previous["first_seen_at"] if previous else now
                payload = dict(normalized, first_seen_at_utc=first_seen, ingested_at_utc=now)
                cur = db.execute("""INSERT INTO revisions
                    (event_id,source_id,fingerprint,observed_at,first_seen_at,ingested_at,
                     change_kind,live_eligible,event_json,raw_json) VALUES (?,?,?,?,?,?,?,?,?,?)""",
                    (payload["event_id"], source_id, fingerprint, payload["observed_at_utc"],
                     first_seen, now, kind, int(live), canonical(payload), canonical(raw)))
                db.execute("INSERT OR REPLACE INTO events VALUES (?,?,?,?)",
                           (payload["event_id"], source_id, payload["observed_at_utc"], cur.lastrowid))
                corrected += bool(previous)
                inserted += not bool(previous)
            status.update(metadata or {})
            status.update(last_success_at_utc=now, last_error=None, consecutive_errors=0,
                          last_poll_complete=bool(complete),
                          baseline_complete=status.get("baseline_complete", False) or bool(complete),
                          last_poll_inserted=inserted, last_poll_corrected=corrected,
                          last_poll_duplicates=duplicates)
            if complete:
                status["last_complete_at_utc"] = now
            db.execute("INSERT OR REPLACE INTO sources VALUES (?,?)", (source_id, canonical(status)))
            db.execute("INSERT INTO polls(source_id,completed_at,complete,details) VALUES (?,?,?,?)",
                       (source_id, now, int(complete), canonical(dict(metadata or {}, inserted=inserted,
                        corrected=corrected, duplicates=duplicates))))
        return dict(inserted=inserted, corrected=corrected, duplicates=duplicates, complete=complete)

    def high_water(self):
        with self.connect() as db:
            return db.execute("SELECT COALESCE(MAX(seq),0) FROM revisions").fetchone()[0]

    def disk_bytes(self):
        return sum(p.stat().st_size for p in Path(self.path).parent.glob(Path(self.path).name + "*") if p.is_file())

    def changes(self, after=0, limit=200):
        with self.connect() as db:
            rows = db.execute("SELECT * FROM revisions WHERE seq>? ORDER BY seq LIMIT ?",
                              (after, limit)).fetchall()
        return [dict(seq=r["seq"], change_kind=r["change_kind"],
                     live_eligible=bool(r["live_eligible"]), event=json.loads(r["event_json"])) for r in rows]

    def events(self, before=None, limit=200, source=None):
        conditions, args = [], []
        if before:
            # Cursor is (observed_at,id); lexical UTC timestamps are canonical.
            conditions.append("(e.observed_at,e.id)<(?,?)")
            args.extend(before)
        if source:
            conditions.append("e.source_id=?")
            args.append(source)
        where = " WHERE " + " AND ".join(conditions) if conditions else ""
        with self.connect() as db:
            rows = db.execute("SELECT r.event_json FROM events e JOIN revisions r ON r.seq=e.revision_seq" +
                              where + " ORDER BY e.observed_at DESC,e.id DESC LIMIT ?", args + [limit]).fetchall()
        return [json.loads(r[0]) for r in rows]

    def status(self):
        with self.connect() as db:
            sources = {r["id"]: json.loads(r["status"]) for r in db.execute("SELECT * FROM sources")}
            count = db.execute("SELECT COUNT(*) FROM events").fetchone()[0]
            for sid, info in sources.items():
                row = db.execute("SELECT COUNT(*),MAX(observed_at) FROM events WHERE source_id=?", (sid,)).fetchone()
                info.update(unique_events=row[0], latest_observation_at_utc=row[1])
        return dict(protocol_version=1, schema_version=3, store_id=self.store_id, event_count=count,
                    high_water_seq=self.high_water(), sources=sources,
                    database_bytes=self.disk_bytes(), collection_soft_limit_bytes=512 * 1024 * 1024)
