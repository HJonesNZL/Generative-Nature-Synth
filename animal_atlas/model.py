"""Source-neutral, versioned events; never infer individual identity from stations."""
import hashlib
import json
import math
import re
from datetime import datetime, timezone, timedelta


def utcnow():
    return datetime.now(timezone.utc)


def stamp(value=None):
    return (value or utcnow()).astimezone(timezone.utc).isoformat(timespec="microseconds").replace("+00:00", "Z")


def parse_time(value):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(?:\.[0-9]{1,6})?(?:Z|[+-][0-9]{2}:[0-9]{2})", value):
        raise ValueError("timestamp requires explicit ISO timezone and microsecond precision")
    if value[-1] != "Z" and (int(value[-5:-3]) > 23 or int(value[-2:]) > 59):
        raise ValueError("invalid timezone offset")
    result = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if result.tzinfo is None:
        raise ValueError("timestamp requires explicit timezone")
    try:
        return result.astimezone(timezone.utc)
    except OverflowError as error:
        raise ValueError("timestamp outside UTC year range") from error


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)


def digest(value):
    return hashlib.sha256(canonical(value).encode()).hexdigest()


def event(source, provider_id, observed, kind, location_kind, lat=None, lon=None, **fields):
    if provider_id is None or not str(provider_id):
        raise ValueError("missing provider event identity")
    when = parse_time(observed)
    if when > utcnow() + timedelta(minutes=5):
        raise ValueError("suspicious future timestamp")
    flags = fields.pop("quality_flags", [])
    if not isinstance(flags, list):
        raise ValueError("quality flags must be an array")
    flags = list(flags)
    if lat is not None or lon is not None:
        if (isinstance(lat, bool) or isinstance(lon, bool) or
                not isinstance(lat, (float, int)) or not isinstance(lon, (float, int)) or
                not math.isfinite(lat) or not math.isfinite(lon) or
                not -90 <= lat <= 90 or not -180 <= lon <= 180):
            raise ValueError("invalid coordinate pair")
    else:
        flags.append("location_missing")
    result = dict(schema_version=1, event_id=source + ":" + str(provider_id),
                  source_id=source, provider_event_id=str(provider_id), provider_record_revision=None,
                  observed_at_utc=stamp(when), provider_published_at_utc=None,
                  event_kind=kind, location_kind=location_kind,
                  latitude=lat, longitude=lon, coordinate_order="latitude,longitude",
                  uncertainty_m=None, species_id=None, scientific_name=None, common_name=None,
                  individual_id=None, deployment_id=None, station_id=None,
                  confidence=None, confidence_meaning=None, quality_flags=flags,
                  provenance_mode="live_source", permissions_status="unresolved",
                  attribution=None)
    result.update(fields)
    validate_event(result)
    return result


EVENT_FIELDS = {
    "schema_version", "event_id", "source_id", "provider_event_id", "provider_record_revision",
    "observed_at_utc", "provider_published_at_utc", "event_kind", "location_kind",
    "latitude", "longitude", "coordinate_order", "uncertainty_m", "species_id", "scientific_name",
    "common_name", "individual_id", "deployment_id", "station_id", "confidence", "confidence_meaning",
    "quality_flags", "provenance_mode", "permissions_status", "attribution", "source_url",
    "provider_scores", "provider_subject_id", "individual_display_name", "first_seen_at_utc", "ingested_at_utc"
}
REQUIRED_EVENT_FIELDS = {"schema_version", "event_id", "source_id", "provider_event_id", "observed_at_utc",
    "event_kind", "location_kind", "latitude", "longitude", "coordinate_order", "provenance_mode"}
KIND_LOCATIONS = {"gps_fix": "individual_fix", "satellite_fix": "individual_fix",
    "acoustic_receiver_detection": "receiver_detection", "acoustic_species_detection": "station_detection",
    "human_observation": "reported_location"}


def event_text(value, maximum=2048):
    if not isinstance(value, str) or not value or "\x00" in value:
        raise ValueError("invalid event text")
    try:
        if len(value.encode("utf-8")) > maximum:
            raise ValueError("event text exceeds capacity")
    except UnicodeError as error:
        raise ValueError("invalid Unicode event text") from error


def event_number(value):
    if type(value) not in (int, float):
        raise ValueError("invalid numeric event field")
    try:
        if not math.isfinite(value):
            raise ValueError("nonfinite event field")
    except OverflowError as error:
        raise ValueError("numeric event field exceeds capacity") from error


def validate_event(value, frozen=False):
    required = REQUIRED_EVENT_FIELDS | ({"first_seen_at_utc", "ingested_at_utc"} if frozen else set())
    if not isinstance(value, dict) or set(value) - EVENT_FIELDS or not required <= set(value):
        raise ValueError("unexpected or missing event fields")
    if type(value["schema_version"]) is not int or value["schema_version"] != 1:
        raise ValueError("unsupported event version")
    text_fields = EVENT_FIELDS - {"schema_version", "latitude", "longitude", "uncertainty_m",
                                 "confidence", "provider_scores", "quality_flags"}
    for key in text_fields:
        item = value.get(key)
        if item is None:
            if key in required: raise ValueError("missing event text")
            continue
        event_text(item, 512 if key.endswith("_id") else 2048)
        if key.endswith("_at_utc"):
            parse_time(item)
    if len(value["source_id"].encode()) > 128 or ":" in value["source_id"]:
        raise ValueError("invalid source namespace")
    if value["event_id"] != value["source_id"] + ":" + value["provider_event_id"]:
        raise ValueError("event identity mismatch")
    for key in ("species_id", "individual_id", "deployment_id", "station_id", "provider_subject_id"):
        item = value.get(key)
        if item is not None and (":" not in item or not all(item.split(":"))):
            raise ValueError("identity requires a namespace")
    if KIND_LOCATIONS.get(value["event_kind"]) != value["location_kind"]:
        raise ValueError("invalid event kind/location pairing")
    if value["event_kind"] == "acoustic_species_detection" and any(value.get(k) is not None for k in ("individual_id", "deployment_id")):
        raise ValueError("station species detection cannot identify an individual or deployment")
    if value["coordinate_order"] != "latitude,longitude" or value["provenance_mode"] not in ("live_source", "imported", "demo"):
        raise ValueError("invalid coordinate order or provenance")
    lat, lon = value["latitude"], value["longitude"]
    if (lat is None) != (lon is None): raise ValueError("incomplete coordinate pair")
    if lat is not None:
        event_number(lat); event_number(lon)
        if not -90 <= lat <= 90 or not -180 <= lon <= 180: raise ValueError("coordinate outside range")
    for key in ("uncertainty_m", "confidence"):
        if value.get(key) is not None:
            event_number(value[key])
    if value.get("uncertainty_m") is not None and value["uncertainty_m"] < 0:
        raise ValueError("negative uncertainty")
    if value.get("confidence") is not None and value.get("confidence_meaning") is None:
        raise ValueError("confidence requires provider-specific meaning")
    flags = value.get("quality_flags", [])
    if not isinstance(flags, list) or len(flags) > 64: raise ValueError("invalid quality flags")
    for flag in flags: event_text(flag, 128)
    scores = value.get("provider_scores", {})
    if not isinstance(scores, dict) or len(scores) > 32: raise ValueError("invalid provider scores")
    for key, score in scores.items():
        event_text(key, 128)
        if score is not None: event_number(score)
    if len(canonical(value).encode()) > 16384:
        raise ValueError("event exceeds capacity")
    return value
