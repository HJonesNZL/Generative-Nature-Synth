"""Read-only adapters. No API keys, paid services, media downloads or private routes."""
import json
import math
import urllib.request
import urllib.error
import urllib.parse
from dataclasses import dataclass
from datetime import timedelta
from email.utils import parsedate_to_datetime
from .model import event, parse_time, utcnow, stamp


class FetchError(Exception):
    def __init__(self, code, retry_after=0):
        super().__init__(code)  # Do not log URLs, bodies, tokens or provider HTML.
        self.retry_after = retry_after


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise FetchError("redirect_not_allowed")


class Transport:
    def __init__(self, timeout=15, max_bytes=8 * 1024 * 1024):
        self.timeout, self.max_bytes = timeout, max_bytes
        self.opener = urllib.request.build_opener(NoRedirect())

    def json(self, url, body=None):
        headers = {"User-Agent": "AnimalAtlas-LocalResearch/0.1", "Accept": "application/json"}
        if body is not None:
            headers["Content-Type"] = "application/json"
        req = urllib.request.Request(url, data=json.dumps(body).encode() if body is not None else None,
                                     headers=headers)
        try:
            with self.opener.open(req, timeout=self.timeout) as response:
                data = response.read(self.max_bytes + 1)
            if len(data) > self.max_bytes:
                raise FetchError("response_too_large")
            return json.loads(data)
        except urllib.error.HTTPError as exc:
            delay = 0
            value = exc.headers.get("Retry-After", "0")
            try:
                delay = max(0, float(value))
            except ValueError:
                try:
                    delay = max(0, (parsedate_to_datetime(value) - utcnow()).total_seconds())
                except (ValueError, TypeError):
                    pass
            if not math.isfinite(delay):
                delay = 0
            delay = min(delay, 86400)
            raise FetchError("http_" + str(exc.code), delay) from None
        except (urllib.error.URLError, TimeoutError, OSError):
            raise FetchError("network_or_tls_error") from None
        except (ValueError, UnicodeError):
            raise FetchError("invalid_json") from None


@dataclass
class Batch:
    records: list
    complete: bool
    metadata: dict


BIRD_QUERY = """query Atlas($first:Int,$after:String,$period:InputDuration,$ne:InputLocation,$sw:InputLocation) {
  detections(first:$first,after:$after,period:$period,ne:$ne,sw:$sw) {
    nodes { id timestamp coords {lat lon} species {id scientificName commonName}
            station {id} confidence score probability certainty }
    pageInfo {endCursor hasNextPage}
  }
}"""


class BirdWeather:
    id = "birdweather"
    min_poll_seconds = 60

    def __init__(self, config, transport=None):
        self.config = config
        self.transport = transport or Transport()

    @staticmethod
    def normalize(raw):
        coords, species, station = raw.get("coords") or {}, raw.get("species") or {}, raw.get("station") or {}
        sid = species.get("id")
        return event("birdweather", raw.get("id"), raw.get("timestamp"),
                     "acoustic_species_detection", "station_detection",
                     coords.get("lat"), coords.get("lon"),
                     species_id="birdweather:species:" + str(sid) if sid is not None else None,
                     scientific_name=species.get("scientificName"), common_name=species.get("commonName"),
                     station_id="birdweather:station:" + str(station["id"]) if station.get("id") is not None else None,
                     confidence=raw.get("confidence"), confidence_meaning="BirdWeather reported confidence; not cross-provider calibrated",
                     provider_scores={k: raw.get(k) for k in ("score", "probability", "certainty")},
                     attribution="BirdWeather and contributing station",
                     source_url="https://app.birdweather.com/",
                     quality_flags=["station_location_not_individual_position", "machine_classification_unverified"])

    def fetch(self, previous, stop):
        now = utcnow()
        lookback = self.config.get("lookback_minutes", 10)
        records, invalid, cursor, seen_cursors = [], 0, None, set()
        complete = False
        # InputDuration.from/to are DATE, not timestamp: use documented rolling windows.
        # They are not snapshot-consistent; completeness below only means pagination exhausted.
        for page in range(self.config.get("max_pages", 10)):
            if stop.is_set():
                raise FetchError("cancelled")
            box = self.config.get("bbox")
            variables = {"first": self.config.get("page_size", 100), "after": cursor,
                         "period": {"count": lookback, "unit": "minute"}}
            if box:
                variables.update(sw={"lat": box[0], "lon": box[1]}, ne={"lat": box[2], "lon": box[3]})
            result = self.transport.json("https://app.birdweather.com/graphql", {"query": BIRD_QUERY, "variables": variables})
            if result.get("errors"):
                raise FetchError("graphql_error")
            try:
                connection = result["data"]["detections"]
                nodes, info = connection["nodes"], connection["pageInfo"]
                if not isinstance(nodes, list) or len(nodes) > self.config.get("page_size", 100):
                    raise ValueError()
                for raw in nodes:
                    try:
                        normalized = self.normalize(raw)
                        if parse_time(normalized["observed_at_utc"]) > now:
                            # Defer records newer than this poll's observation boundary.
                            continue
                        records.append((normalized, raw))
                    except (ValueError, TypeError, KeyError, AttributeError):
                        invalid += 1
                if not isinstance(info.get("hasNextPage"), bool):
                    raise ValueError()
                if not info["hasNextPage"]:
                    complete = True
                    break
                cursor = info["endCursor"]
                if not cursor or cursor in seen_cursors:
                    raise ValueError()
                seen_cursors.add(cursor)
            except (KeyError, TypeError, ValueError):
                raise FetchError("schema_or_pagination_error") from None
        start = now - timedelta(minutes=lookback)
        prior = previous.get("last_complete_at_utc")
        gap = bool(prior and parse_time(prior) < start)
        return Batch(records, complete, dict(technical_status="endpoint_tested", cost="zero_charge_public_route",
            permissions_status="unresolved_commercial_reuse", region=self.config.get("bbox", "global"),
            invalid_records=invalid, pages_fetched=page + 1,
            observation_window_start_utc=stamp(start), observation_window_end_utc=stamp(now),
            possible_unrecoverable_gap=gap or previous.get("possible_unrecoverable_gap", False),
            completeness="rolling_window_best_effort; no publication cursor; late uploads may be missed",
            pagination_exhausted=complete, freshness_claim="inspect latest_observation_at_utc",
            publication_lag_seconds=None))


class NZSharks:
    """Public-map latest-fix snapshots, NOT a complete track-history connector."""
    id = "nzsharks"
    min_poll_seconds = 900

    def __init__(self, config, transport=None):
        self.config = config
        self.transport = transport or Transport()

    def fetch(self, previous, stop):
        bootstrap = self.transport.json("https://map.sustainableoceansociety.co.nz/public/bootstrap.json")
        config_url = bootstrap.get("configUrl")
        if config_url != "https://map.sustainableoceansociety.co.nz/public/configs/config.json":
            raise FetchError("public_config_changed_review_required")
        cfg = self.transport.json(config_url)
        # Fail closed if public configuration points at a new installation.
        if cfg.get("server") != "ermap-server.pamdas.org" or cfg.get("public_name") != "nzsharkman":
            raise FetchError("public_server_changed_review_required")
        group = cfg.get("subject_group")
        if not isinstance(group, str) or not group:
            raise FetchError("subject_group_missing")
        url = "https://ermap-server.pamdas.org/nzsharkman/api/v1.0/subjects?" + urllib.parse.urlencode({"subject_group": group})
        if stop.is_set():
            raise FetchError("cancelled")
        response = self.transport.json(url)
        try:
            subjects = response["data"]["data"]
            if not isinstance(subjects, list) or len(subjects) > 1000:
                raise ValueError()
        except (KeyError, TypeError, ValueError):
            raise FetchError("subject_schema_changed") from None
        records, invalid = [], 0
        for subject in subjects:
            try:
                point = subject.get("last_position")
                if not point:
                    continue
                sid = subject["id"]
                geometry, props = point["geometry"], point["properties"]
                if geometry["type"] != "Point" or len(geometry["coordinates"]) != 2:
                    raise ValueError()
                lon, lat = geometry["coordinates"]
                observed = stamp(parse_time(props["DateTime"]))
                named = cfg.get("subjects", {}).get(sid, {})
                # Same timestamp + subject means same fix; position corrections become revisions.
                normalized = event(self.id, sid + ":" + observed, observed, "satellite_fix", "individual_fix",
                    lat, lon, scientific_name="Carcharodon carcharias", common_name="Great white shark",
                    species_id="nzsharks:species:great_white_shark",
                    individual_id=None, deployment_id=None, provider_subject_id="nzsharks:subject:" + sid,
                    individual_display_name=named.get("animal_name") or subject.get("name"),
                    quality_flags=["identity_reconciliation_pending", "position_accuracy_unknown"],
                    attribution="Sustainable Oceans Society / The Great White App",
                    source_url="https://map.sustainableoceansociety.co.nz/")
                # Save only source fields needed to establish this fix, not HTML/images/config tokens.
                records.append((normalized, dict(subject_id=sid, observed_at=props["DateTime"], coordinates=[lon, lat])))
            except (KeyError, TypeError, ValueError, AttributeError):
                invalid += 1
        return Batch(records, True, dict(technical_status="endpoint_tested", cost="zero_charge_public_route",
            permissions_status="unresolved_public_client_route", invalid_records=invalid,
            completeness="latest_position_snapshots_only; intermediate fixes can be missed",
            publication_lag_seconds=None, freshness_claim="inspect latest_observation_at_utc"))
