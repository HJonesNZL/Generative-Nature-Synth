"""Frozen, raw-free event sessions. Collection is never required for replay."""
import hashlib
import json
import os
import tempfile
import uuid
from datetime import timedelta
from pathlib import Path
from .integrity import strict_json, verify, seal, projection, SESSION_FIELDS, SEAL_FIELDS
from .model import canonical, stamp, utcnow, parse_time, validate_event

MAX_EVENTS = 50000
MAX_BUNDLE_BYTES = 64 * 1024 * 1024
def clean_event(value):
    return validate_event(value, frozen=True)


def content_hash(rows):
    result = hashlib.sha256()
    for row in rows:
        result.update((canonical(row) + "\n").encode())
    return result.hexdigest()


class Sessions:
    def __init__(self, store):
        self.store = store

    @staticmethod
    def options(name, sources, seed):
        if not isinstance(name, str) or not 1 <= len(name.strip()) <= 128:
            raise ValueError("session name requires 1-128 characters")
        if type(seed) is not int or not 0 <= seed < 2**63:
            raise ValueError("seed outside integer range")
        if sources is not None and (not isinstance(sources, list) or len(sources) > 30 or
                any(not isinstance(s, str) or not 1 <= len(s) <= 128 for s in sources)):
            raise ValueError("invalid source filter")
        return dict(sources=sorted(set(sources)) if sources is not None else None,
                    seed=seed, software_version="0.2.0", event_schema_version=1,
                    selection_policy="new_live_eligible_revisions", permissions="local_archive_only")

    def start(self, name, sources=None, seed=0, duration_seconds=None):
        options = self.options(name, sources, seed)
        if duration_seconds is not None and (type(duration_seconds) is not int or not 1 <= duration_seconds <= 86400):
            raise ValueError("duration requires 1-86400 seconds")
        sid = str(uuid.uuid4())
        now = utcnow()
        intended = stamp(now + timedelta(seconds=duration_seconds)) if duration_seconds else None
        with self.store.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            if db.execute("SELECT COUNT(*) FROM sessions WHERE state='recording'").fetchone()[0] >= 8:
                raise ValueError("recording session capacity reached")
            high = db.execute("SELECT COALESCE(MAX(seq),0) FROM revisions").fetchone()[0]
            db.execute("""INSERT INTO sessions(id,name,state,created_at,start_seq,intended_end_at,origin,options_json)
                          VALUES (?,?,'recording',?,?,?,'collection',?)""",
                       (sid, name.strip(), stamp(now), high, intended, canonical(options)))
        return self.get(sid)

    def _materialize(self, db, sid, rows, state, end_seq):
        frozen, size, truncated = [], 0, False
        for row in rows:
            if len(frozen) >= MAX_EVENTS:
                truncated = True
                break
            payload = clean_event(strict_json(row["event_json"]))
            size += len(canonical(payload).encode())
            if size > MAX_BUNDLE_BYTES - 1024 * 1024:
                truncated = True
                break
            frozen.append(dict(original_seq=row["seq"], event=payload))
        session = db.execute("SELECT * FROM sessions WHERE id=?", (sid,)).fetchone()
        options = json.loads(session["options_json"])
        options["capture_truncated"] = truncated
        finished = stamp()
        if state == "interrupted":
            options["interruption_detected_at_utc"] = finished
            options["end_time_quality"] = "last_captured_arrival; actual process exit unknown"
            finished = max([session["created_at"]] + [item["event"]["first_seen_at_utc"] for item in frozen])
        bounds = {}
        for mode, key in (("observed", "observed_at_utc"), ("arrival", "first_seen_at_utc")):
            times = [item["event"][key] for item in frozen]
            if mode == "arrival" and session["origin"] == "collection":
                bounds[mode] = [session["created_at"], max([finished, session["created_at"]] + times)]
            elif times:
                bounds[mode] = [min(times), max(times)]
            else:
                bounds[mode] = [session["created_at"], session["created_at"]]
        options["timeline_bounds"] = bounds
        db.execute("UPDATE sessions SET options_json=? WHERE id=?", (canonical(options), sid))
        checksum = content_hash(frozen)
        db.executemany("INSERT INTO session_events VALUES (?,?,?,?)",
                       [(sid, i, item["original_seq"], canonical(item["event"])) for i, item in enumerate(frozen)])
        db.execute("UPDATE sessions SET state=?,ended_at=?,end_seq=?,event_count=?,content_hash=? WHERE id=?",
                   (state, finished, end_seq, len(frozen), checksum, sid))
        seal(db, sid)

    def stop(self, sid, interrupted=False):
        with self.store.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            session = db.execute("SELECT * FROM sessions WHERE id=?", (sid,)).fetchone()
            if session is None:
                raise KeyError(sid)
            if session["state"] != "recording":
                return self._summary(session)
            high = db.execute("SELECT COALESCE(MAX(seq),0) FROM revisions").fetchone()[0]
            options = json.loads(session["options_json"])
            query = "SELECT seq,event_json FROM revisions WHERE seq>? AND seq<=? AND live_eligible=1"
            args = [session["start_seq"], high]
            sources = options["sources"]
            if sources is not None:
                query += " AND source_id IN (" + ",".join("?" for _ in sources) + ")" if sources else " AND 0"
                args.extend(sources)
            query += " ORDER BY seq"
            self._materialize(db, sid, db.execute(query, args), "interrupted" if interrupted else "frozen", high)
        return self.get(sid)

    def snapshot(self, name, sources=None, seed=0, observed_from=None, observed_to=None):
        options = self.options(name, sources, seed)
        options["selection_policy"] = "latest_event_revisions_at_snapshot"
        lower = stamp(parse_time(observed_from)) if observed_from else None
        upper = stamp(parse_time(observed_to)) if observed_to else None
        if lower and upper and lower >= upper:
            raise ValueError("observation range must be increasing")
        options.update(observed_from=lower, observed_to=upper)
        sid = str(uuid.uuid4())
        with self.store.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            high = db.execute("SELECT COALESCE(MAX(seq),0) FROM revisions").fetchone()[0]
            db.execute("""INSERT INTO sessions(id,name,state,created_at,start_seq,origin,options_json)
                          VALUES (?,?,'building',?,0,'archive_snapshot',?)""", (sid, name.strip(), stamp(), canonical(options)))
            query = "SELECT r.seq,r.event_json FROM events e JOIN revisions r ON r.seq=e.revision_seq WHERE 1"
            args = []
            for boundary, comparison in ((lower, ">="), (upper, "<")):
                if boundary:
                    query += " AND r.observed_at" + comparison + "?"
                    args.append(boundary)
            if sources is not None:
                query += " AND r.source_id IN (" + ",".join("?" for _ in sources) + ")" if sources else " AND 0"
                args.extend(sources)
            self._materialize(db, sid, db.execute(query + " ORDER BY r.seq", args), "frozen", high)
        return self.get(sid)

    @staticmethod
    def _summary(row):
        result = dict(row)
        result["options"] = strict_json(result.pop("options_json"))
        if result["state"] in ("frozen", "interrupted"):
            result["metadata_integrity"] = verify(result)
        return result

    def get(self, sid):
        with self.store.connect() as db:
            row = db.execute("SELECT * FROM sessions WHERE id=?", (sid,)).fetchone()
            if row is None:
                raise KeyError(sid)
            return self._summary(row)

    def list(self, limit=100):
        if type(limit) is not int or not 1 <= limit <= 100:
            raise ValueError("session list limit exceeded")
        with self.store.connect() as db:
            return [self._summary(row) for row in db.execute("SELECT * FROM sessions ORDER BY created_at DESC,id LIMIT ?", (limit,))]

    def rows(self, sid, after=-1, limit=500):
        self.get(sid)
        if type(after) is not int or after < -1 or type(limit) is not int or not 1 <= limit <= MAX_EVENTS:
            raise ValueError("invalid session cursor")
        with self.store.connect() as db:
            return [dict(ordinal=row["ordinal"], original_seq=row["original_seq"], event=json.loads(row["event_json"]))
                    for row in db.execute("SELECT * FROM session_events WHERE session_id=? AND ordinal>? ORDER BY ordinal LIMIT ?", (sid, after, limit))]

    def finish_due(self):
        with self.store.connect() as db:
            due = [r[0] for r in db.execute("SELECT id FROM sessions WHERE state='recording' AND intended_end_at<=?", (stamp(),))]
        return [self.stop(sid) for sid in due]

    def recover(self):
        """Called with exclusive collector ownership before network workers start."""
        with self.store.connect() as db:
            active = [row[0] for row in db.execute("SELECT id FROM sessions WHERE state='recording'")]
        return [self.stop(sid, interrupted=True) for sid in active]

    def active(self):
        with self.store.connect() as db:
            return [row[0] for row in db.execute("SELECT id FROM sessions WHERE state='recording'")]

    def frozen(self, sid):
        """Verify metadata and canonical payloads in one worker-owned read snapshot."""
        with self.store.connect() as db:
            db.execute("BEGIN")
            row = db.execute("SELECT * FROM sessions WHERE id=?", (sid,)).fetchone()
            if row is None:
                raise KeyError(sid)
            session = self._summary(row)
            if session["state"] not in ("frozen", "interrupted"):
                raise ValueError("stop recording before replay/export")
            rows, size = [], 0
            for row in db.execute("SELECT * FROM session_events WHERE session_id=? ORDER BY ordinal LIMIT 50001", (sid,)):
                if row["ordinal"] != len(rows) or len(rows) >= MAX_EVENTS or type(row["original_seq"]) is not int or not 1 <= row["original_seq"] < 2**63:
                    raise ValueError("invalid frozen ordinal/sequence/count")
                payload = row["event_json"]
                if not isinstance(payload, str) or len(payload.encode()) > 16384:
                    raise ValueError("invalid frozen event size/type")
                size += len(payload.encode())
                if size > MAX_BUNDLE_BYTES:
                    raise ValueError("frozen session exceeds capacity")
                value = clean_event(strict_json(payload))
                if payload != canonical(value):
                    raise ValueError("frozen event is not canonical")
                rows.append(dict(original_seq=row["original_seq"], event=value))
            if len(rows) != session["event_count"] or content_hash(rows) != session["content_hash"]:
                raise ValueError("session integrity check failed")
            return session, rows

    def bundle(self, sid):
        session, rows = self.frozen(sid)
        result = dict(format="animal-atlas-session", bundle_version=2, session=session, events=rows,
                      redistribution_permission="not_established; local archive only")
        result["bundle_sha256"] = hashlib.sha256(canonical(result).encode()).hexdigest()
        if len(canonical(result).encode()) > MAX_BUNDLE_BYTES:
            raise ValueError("bundle exceeds 64 MiB")
        return result

    def export(self, sid, path):
        payload = canonical(self.bundle(sid)).encode()
        path = Path(path)
        # Never overwrite an existing piece; publish only a fully flushed file.
        fd, temp = tempfile.mkstemp(prefix=".atlas-export-", dir=str(path.parent))
        try:
            with os.fdopen(fd, "wb") as out:
                out.write(payload)
                out.flush()
                os.fsync(out.fileno())
            os.link(temp, str(path))
        finally:
            os.unlink(temp)
        return dict(path=str(path.resolve()), bytes=len(payload))

    def import_bundle(self, path):
        with Path(path).open("rb") as source:
            payload = source.read(MAX_BUNDLE_BYTES + 1)
        if len(payload) > MAX_BUNDLE_BYTES:
            raise ValueError("bundle exceeds capacity")
        bundle = strict_json(payload)
        if not isinstance(bundle, dict):
            raise ValueError("invalid bundle object")
        if set(bundle) != {"format", "bundle_version", "session", "events", "redistribution_permission", "bundle_sha256"}:
            raise ValueError("unexpected bundle fields")
        checksum = bundle.pop("bundle_sha256")
        if bundle["format"] != "animal-atlas-session" or type(bundle["bundle_version"]) is not int or bundle["bundle_version"] not in (1, 2) or hashlib.sha256(canonical(bundle).encode()).hexdigest() != checksum:
            raise ValueError("invalid bundle or checksum")
        original, rows = bundle["session"], bundle["events"]
        if not isinstance(rows, list) or len(rows) > MAX_EVENTS:
            raise ValueError("too many session events")
        allowed = SESSION_FIELDS | (SEAL_FIELDS | {"metadata_integrity"} if bundle["bundle_version"] == 2 else set())
        if not isinstance(original, dict) or set(original) != allowed:
            raise ValueError("unexpected session fields")
        original_integrity = verify(original)
        options = dict(original["options"])
        options.setdefault("original_session", dict(
            {key: value for key, value in projection(original).items() if key != "options"},
            metadata_integrity=original_integrity))
        for row in rows:
            if not isinstance(row, dict) or set(row) != {"original_seq", "event"} or type(row["original_seq"]) is not int or not 1 <= row["original_seq"] < 2**63:
                raise ValueError("invalid frozen event")
            clean_event(row["event"])
        if content_hash(rows) != original["content_hash"] or len(rows) != original["event_count"]:
            raise ValueError("event checksum/count mismatch")
        sid = str(uuid.uuid4())
        with self.store.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            db.execute("""INSERT INTO sessions(id,name,state,created_at,ended_at,start_seq,end_seq,origin,options_json,event_count,content_hash)
                VALUES (?,?,?,?,?,0,0,'imported_bundle',?,?,?)""",
                (sid, original["name"], original["state"], stamp(), stamp(), canonical(options), len(rows), content_hash(rows)))
            db.executemany("INSERT INTO session_events VALUES (?,?,?,?)",
                           [(sid, i, row["original_seq"], canonical(row["event"])) for i, row in enumerate(rows)])
            seal(db, sid)
        return self.get(sid)
