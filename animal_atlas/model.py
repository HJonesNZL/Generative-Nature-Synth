"""Source-neutral, versioned events; never infer individual identity from stations."""
import hashlib
import json
import math
from datetime import datetime, timezone, timedelta


def utcnow():
    return datetime.now(timezone.utc)


def stamp(value=None):
    return (value or utcnow()).astimezone(timezone.utc).isoformat(timespec="microseconds").replace("+00:00", "Z")


def parse_time(value):
    if not isinstance(value, str):
        raise ValueError("missing timestamp")
    result = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if result.tzinfo is None:
        raise ValueError("timestamp requires explicit timezone")
    return result.astimezone(timezone.utc)


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
    flags = list(fields.pop("quality_flags", []))
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
    canonical(result)
    return result
