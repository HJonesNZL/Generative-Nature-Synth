"""Bounded session metadata seals. Checksums detect changes, not authorship."""
import hashlib
import json
import re
from .model import canonical, parse_time

SESSION_FIELDS = {"id", "name", "state", "created_at", "ended_at", "start_seq", "end_seq",
                  "intended_end_at", "origin", "options", "event_count", "content_hash"}
SEAL_FIELDS = {"envelope_version", "envelope_json", "envelope_hash"}
OPTION_FIELDS = {"sources", "seed", "software_version", "event_schema_version", "selection_policy",
                 "permissions", "capture_truncated", "timeline_bounds", "observed_from", "observed_to",
                 "interruption_detected_at_utc", "end_time_quality", "original_session",
                 "window_policy", "window_policy_version"}
MAX_ENVELOPE_BYTES = 32768


def strict_json(payload):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate JSON key")
            result[key] = value
        return result
    def invalid(value):
        raise ValueError("nonfinite JSON number")
    try:
        return json.loads(payload, object_pairs_hook=pairs, parse_constant=invalid)
    except (RecursionError, UnicodeError) as error:
        raise ValueError("invalid JSON encoding or depth") from error


def checksum(payload):
    return hashlib.sha256(payload.encode()).hexdigest()


def validate_metadata(session):
    if set(session) != SESSION_FIELDS:
        raise ValueError("unexpected session metadata fields")
    for key in ("id", "name", "origin"):
        if not isinstance(session[key], str) or not 1 <= len(session[key]) <= 128:
            raise ValueError("invalid session text")
    if session["state"] not in ("frozen", "interrupted"):
        raise ValueError("session is not frozen")
    for key in ("created_at", "ended_at", "intended_end_at"):
        if session[key] is not None:
            parse_time(session[key])
        elif key == "created_at":
            raise ValueError("missing creation time")
    for key in ("start_seq", "end_seq", "event_count"):
        if type(session[key]) is not int or not 0 <= session[key] < 2**63:
            raise ValueError("invalid session count/sequence")
    if session["event_count"] > 50000 or not isinstance(session["content_hash"], str) or not re.fullmatch("[0-9a-f]{64}", session["content_hash"]):
        raise ValueError("invalid event count/checksum")
    options = session["options"]
    if not isinstance(options, dict) or set(options) - OPTION_FIELDS or len(canonical(options)) > 16384:
        raise ValueError("invalid session options")
    if type(options.get("seed")) is not int or not 0 <= options["seed"] < 2**63:
        raise ValueError("invalid seed")
    if type(options.get("event_schema_version")) is not int or options["event_schema_version"] != 1:
        raise ValueError("unsupported event version")
    sources = options.get("sources")
    if sources is not None and (not isinstance(sources, list) or len(sources) > 30 or
            any(not isinstance(s, str) or not 1 <= len(s) <= 128 for s in sources)):
        raise ValueError("invalid sources")
    for key in ("software_version", "selection_policy", "permissions"):
        if not isinstance(options.get(key), str) or not 1 <= len(options[key]) <= 256:
            raise ValueError("invalid option text")
    if type(options.get("capture_truncated", False)) is not bool:
        raise ValueError("invalid truncation flag")
    bounds = options.get("timeline_bounds")
    if not isinstance(bounds, dict) or set(bounds) != {"observed", "arrival"}:
        raise ValueError("missing timeline bounds")
    for pair in bounds.values():
        if not isinstance(pair, list) or len(pair) != 2 or parse_time(pair[0]) > parse_time(pair[1]):
            raise ValueError("invalid timeline bounds")
    for key in ("observed_from", "observed_to", "interruption_detected_at_utc"):
        if options.get(key) is not None:
            parse_time(options[key])
    original = options.get("original_session")
    if original is not None:
        fields = SESSION_FIELDS - {"options"}
        if not isinstance(original, dict) or set(original) != fields | {"metadata_integrity"}:
            raise ValueError("invalid original provenance")
        validate_metadata(dict({key: original[key] for key in fields}, options={
            key: value for key, value in options.items() if key != "original_session"}))
        if original["metadata_integrity"] not in ("sha256-envelope-v1-verified", "legacy-metadata-unverified"):
            raise ValueError("invalid original integrity state")


def projection(session):
    return {key: session[key] for key in SESSION_FIELDS}


def verify(session):
    metadata = projection(session)
    validate_metadata(metadata)
    version = session.get("envelope_version", 0)
    if type(version) is not int:
        raise ValueError("invalid envelope version")
    if version == 0:
        if session.get("envelope_json") is not None or session.get("envelope_hash") is not None:
            raise ValueError("partial legacy envelope")
        return "legacy-metadata-unverified"
    if version != 1:
        raise ValueError("unsupported metadata envelope version")
    payload = session.get("envelope_json")
    if not isinstance(payload, str) or len(payload.encode()) > MAX_ENVELOPE_BYTES:
        raise ValueError("missing or excessive metadata envelope")
    expected = {"version": 1, "session": metadata}
    if checksum(payload) != session.get("envelope_hash") or strict_json(payload) != expected or payload != canonical(expected):
        raise ValueError("session metadata integrity failure")
    return "sha256-envelope-v1-verified"


def seal(db, sid):
    row = dict(db.execute("SELECT * FROM sessions WHERE id=?", (sid,)).fetchone())
    row["options"] = strict_json(row.pop("options_json"))
    metadata = projection(row)
    validate_metadata(metadata)
    payload = canonical({"version": 1, "session": metadata})
    if len(payload.encode()) > MAX_ENVELOPE_BYTES:
        raise ValueError("metadata envelope exceeds capacity")
    db.execute("UPDATE sessions SET envelope_version=1,envelope_json=?,envelope_hash=? WHERE id=?",
               (payload, checksum(payload), sid))
