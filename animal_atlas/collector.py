import json
import math
import random
import threading
from .model import stamp, digest
from .sources import BirdWeather, NZSharks, FetchError


DEFAULT_CONFIG = {
    "birdweather": {"enabled": True, "poll_seconds": 60, "lookback_minutes": 10,
                    "page_size": 100, "max_pages": 10, "bbox": [-48, 166, -34, 179.9]},
    "nzsharks": {"enabled": True, "poll_seconds": 900}
}


def load_config(path=None):
    cfg = json.loads(json.dumps(DEFAULT_CONFIG)) if path is None else json.loads(path.read_text())
    if not isinstance(cfg, dict) or set(cfg) - {"birdweather", "nzsharks"}:
        raise ValueError("unknown source; paid/custom endpoints are not supported")
    for sid, item in cfg.items():
        allowed = set(DEFAULT_CONFIG[sid])
        if not isinstance(item, dict) or set(item) - allowed:
            raise ValueError("unknown source setting")
        merged = dict(DEFAULT_CONFIG[sid], **item)
        if not isinstance(merged["enabled"], bool):
            raise ValueError("enabled must be boolean")
        minimum = 60 if sid == "birdweather" else 900
        seconds = merged["poll_seconds"]
        if isinstance(seconds, bool) or not isinstance(seconds, (int, float)) or not math.isfinite(seconds) or not minimum <= seconds <= 86400:
            raise ValueError("poll interval out of bounds")
        if sid == "birdweather":
            for key, low, high in [("page_size", 1, 100), ("max_pages", 1, 10), ("lookback_minutes", 1, 60)]:
                if type(merged[key]) is not int or not low <= merged[key] <= high:
                    raise ValueError(key + " out of bounds")
            box = merged["bbox"]
            if box is not None:
                if (not isinstance(box, list) or len(box) != 4 or
                    any(type(x) not in (int, float) or not math.isfinite(x) for x in box) or
                    not -90 <= box[0] < box[2] <= 90 or not -180 <= box[1] < box[3] <= 180):
                    raise ValueError("bbox must be south,west,north,east; split antimeridian regions")
        cfg[sid] = merged
    return cfg


class Collector:
    def __init__(self, store, config, stop=None):
        self.store, self.config = store, config
        self.stop = stop or threading.Event()
        self.adapters = {sid: cls(config[sid]) for sid, cls in (("birdweather", BirdWeather), ("nzsharks", NZSharks))
                         if sid in config and config[sid]["enabled"]}

    def poll(self, sid):
        scope = digest({k: v for k, v in self.config[sid].items() if k in ("bbox", "lookback_minutes")})
        if self.store.source(sid).get("scope_fingerprint") != scope:
            self.store.update_source(sid, scope_fingerprint=scope, baseline_complete=False)
        self.store.update_source(sid, last_attempt_at_utc=stamp(), enabled=True)
        previous = self.store.source(sid)
        try:
            batch = self.adapters[sid].fetch(previous, self.stop)
            result = self.store.ingest(sid, batch.records, batch.complete, batch.metadata)
            return result, 0
        except FetchError as exc:
            failures = previous.get("consecutive_errors", 0) + 1
            delay = max(exc.retry_after, min(3600, 60 * 2 ** min(failures - 1, 6)))
            self.store.update_source(sid, last_error=str(exc), consecutive_errors=failures,
                                     retry_delay_seconds=delay, last_poll_complete=False)
            return {"error": str(exc)}, delay
        except Exception:
            # Unexpected schema/storage failures must not leave a source falsely healthy.
            self.store.update_source(sid, last_error="internal_or_storage_error",
                consecutive_errors=previous.get("consecutive_errors", 0) + 1, last_poll_complete=False)
            return {"error": "internal_or_storage_error"}, 60

    def _worker(self, sid):
        interval = self.config[sid]["poll_seconds"]
        while not self.stop.is_set():
            try:
                result, retry = self.poll(sid)
                print(json.dumps({"source": sid, **result}), flush=True)
            except Exception:
                # Isolate failed source/storage operations; do not kill other source workers.
                retry = 60
                print(json.dumps({"source": sid, "error": "internal_or_storage_error"}), flush=True)
            self.stop.wait(max(interval, retry) + random.uniform(0, 3))

    def start(self):
        threads = []
        for sid in self.adapters:
            thread = threading.Thread(target=self._worker, args=(sid,), name="collector-" + sid, daemon=True)
            thread.start()
            threads.append(thread)
        return threads
