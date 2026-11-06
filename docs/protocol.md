# Application import protocol v1

The v0.2 backend preserves the live import protocol and adds session recording, snapshot and replay routes documented in `sessions-and-replay.md`. Storage schema is now v2; individual normalized events remain schema v1. Session HTTP mutations use the same authentication and browser-Origin denial.

Transport: HTTP/1.0 JSON, bound exclusively to `127.0.0.1`. The collector publishes its dynamic port and store identity in `.state/connection.json`. Read `.state/api.token` locally and send `Authorization: Bearer <token>`. Never put it in a URL, preset, event export, telemetry or source control. The token and descriptor are mode 0600; the state directory is 0700.

Only the exact `Host: 127.0.0.1:<port>` is accepted. Browser Origin requests are denied and CORS is not enabled. A future embedded browser should talk through its native bridge rather than expose the local token to page scripts. This protects the loopback service from ordinary cross-site access; it does not defend against malicious software already running as this OS user.

## Read endpoints

- `GET /v1/status`: schema/protocol version, `store_id`, revision high water, event counts, source health, latest observation, collection settings and storage use. Publication lag stays null without provider evidence.
- `GET /v1/events?limit=200&source=birdweather&before=<opaque cursor>`: most recent normalized event versions, observation time descending with stable ID tie-break. `source` and `before` are optional. `next_cursor` pages older events. Pages are not a frozen snapshot during concurrent ingestion/correction; use the saved-session API for immutable material.
- `GET /v1/changes?after_seq=123&limit=200&wait=25&store_id=<uuid>`: durable change log, ascending sequence. `wait` is integer 0–25 seconds, limit 1–500. Query store identity is recommended; the bundled client supplies it. Responses include `changes`, `next_seq`, `high_water_seq` and `store_id`.

Example change shape:

```json
{
  "seq": 124,
  "change_kind": "new",
  "live_eligible": true,
  "event": {
    "schema_version": 1,
    "event_id": "birdweather:provider-id",
    "source_id": "birdweather",
    "event_kind": "acoustic_species_detection",
    "location_kind": "station_detection",
    "individual_id": null,
    "observed_at_utc": "2026-11-06T04:00:00.000000Z"
  }
}
```

This shortened protocol illustration is not a real event fixture. Full event records include coordinates, timestamps, provenance, confidence interpretation, attribution and quality flags.

## Consumer contract

1. Open `/v1/status`, retain `store_id`, and decide where to start. For future-only listening use `high_water_seq`; for restoration use the consumer's last saved sequence.
2. Request changes after that sequence. Baseline/correction records update the UI; only `live_eligible=true` is a candidate musical trigger. Musical filtering, quotas and scheduling are separate responsibilities.
3. Admit/process returned changes on a worker thread. Persist your own acknowledgement after handling them. Advance to `next_seq`, **never** directly to `high_water_seq` (a batch may be truncated).
4. Repeat. Delivery is at-least-once across reconnect. Deduplicate by `(store_id,seq)` in the consumer, including crash recovery. Different clients have independent acknowledgements.
5. On network failure, reload the descriptor/token and reconnect with bounded backoff. On 409 `store_changed` or `cursor_ahead_of_store`, explicitly reconcile/reset; never silently skip records. The CLI watch currently exits on connection failure; the client module exposes these errors for a GUI reconnect controller.

No network, JSON parsing, database operations, polling, memory allocation or HTTP client work belongs on an audio callback. A future native bridge must convert accepted changes on a worker into fixed-capacity musical messages. This backend delivers data, not audio-time guarantees. Expected live latency is provider publication delay plus poll interval plus bounded request time.

## Failure responses

400 invalid bounds/cursor, 401 missing/wrong token, 403 nonlocal host/browser Origin, 404 unknown route, 409 inconsistent store/cursor, 503 unavailable storage. At the 12-request concurrency limit new connections close; clients retry with jitter. Requests have a socket timeout. API responses contain neither provider raw payloads nor credentials.
