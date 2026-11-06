# Recording and replay — v0.3

M1 adds schema 3 metadata seals, bundle v2 and provenance-preserving imports. Old sessions remain explicitly metadata-unverified. See [M1 format/verification notes](milestones/M1_VERIFIED_PIECE.md).

A snapshot with both `--observed-from` and `--observed-to` preserves that full half-open interval by default, including empty material. Use `--window-policy trim_to_material` to trim; `preserve_requested` requires both boundaries. The local snapshot API accepts the same `window_policy` values. Replay reports the saved policy, `capture_truncated` and metadata integrity. Empty records are not evidence of ecological silence. Arrival replay uses separate receipt-time bounds.

Everything in this milestone is local and uses the Python standard library. No hosted storage, paid SDK, account, API key, or subscription is introduced.

## Use without typing commands

Open **Animal Atlas.app** in the project folder, or double-click **Start Animal Atlas.command**. The app is a small local launcher that opens a numbered Terminal menu, not the future atlas GUI. Select a one/two-hour recording, collect until stopped, inspect sessions, snapshot cached bird events, preview replay timing, or export the latest saved session. The launcher shell and app manifest have been checked; opening it through Finder has not been manually tested. This locally generated launcher is unsigned and is not a distributable packaged runtime. It uses the already installed `python3`.

One/two-hour options start the collector, create a recording, and stop automatically. Control-C requests an earlier clean shutdown. A Mac that sleeps or disconnects can still miss upstream records; the launcher does not prevent sleep. A concurrent collector for the same state directory is rejected.

A saved sample from actual data is already in the `.state` session library and a private bundle is in `Sessions/`; `evidence/session-milestone.json` gives its ID, counts and replay proof. Both source data and session bundles remain local research archives, not cleared redistributable datasets.

## Two ways to create a piece

**Recording** captures newly discovered, live-eligible events committed after recording starts and through the finalisation sequence. Initial provider baselines, provider corrections and events committed before starting are excluded. The exact original revision seen on arrival is preserved even if its coordinates are subsequently corrected. Multiple events in one provider batch may legitimately share an arrival timestamp. No notes or synthesis controls are recorded yet: this is an event session, not a complete musical performance.

**Archive snapshot** freezes the latest stored version of each selected event at the snapshot transaction. It can include baseline events and old shark positions. Source filters and an optional half-open observation range (`from <= observed < to`) are explicit. A snapshot is labelled `archive_snapshot`; freezing old records never creates new live arrivals in the collector.

Both operations use one SQLite write transaction for the boundary and frozen material. The collector and session writes serialize through SQLite. A failed transaction leaves no half-built session. Session payloads exclude source raw responses and configuration credentials.

## Persistence and recovery

Database schema v2 adds `sessions` and `session_events`, preserving existing events/revisions when migrating from v1. Saved event copies, original sequence IDs, options, seeds, time bounds and SHA-256 content hashes are stored independently of mutable latest-event pointers.

The states are `recording`, `frozen`, and `interrupted`; `building` is transaction-internal and cannot survive a committed snapshot. Only frozen/interrupted sessions replay. Stop is idempotent. Recordings can specify a duration of 1–86,400 seconds; the serving process checks deadlines four times per second, subject to SQLite/network/OS scheduling. Actual capture boundaries are revision sequences, not an assertion of perfect wall-clock duration. UTC deadlines can be affected by system clock changes.

On clean server shutdown, pending recordings are frozen after workers are asked to stop. On the next server startup, leftover recording sessions are frozen as `interrupted` before source workers start. They are not silently resumed. Recovery marks its end time as the last captured arrival, with the true process exit time unknown; it does not invent collection coverage during downtime.

At most eight recordings can be active. A session stores at most 50,000 events and approximately 63 MiB of serialized event material. If admission reaches either bound, it freezes the admitted prefix with `options.capture_truncated=true`; it never silently claims complete capture. The event-count limit is a prototype resource bound, not a source-completeness guarantee. There is no session delete/prune command yet.

## Deterministic data replay

The pure `Timeline` engine supports:

- `observed`: order by the animal event's observation time.
- `arrival`: order by its first-seen collection time.
- Speeds from 0.01x to 10,000x using exact rational arithmetic and integer microseconds.
- Stable simultaneous-event ordering by original revision sequence then event ID.
- Paginated replay plans, duration, a settings fingerprint, and peak events in a sliding second.
- Half-open sample-block windows with declared rounding down to sample indices.
- Bounded block admission with an explicit `omitted` count.

An observed timeline begins at its first selected observation. An arrival recording preserves leading/trailing collection silence within its saved bounds. Archive snapshots use their first/last arrival times. Imported bundles preserve both sets of bounds and produce the same replay plans.

At 60x, an event four hours after the first is scheduled four minutes after it. The reported duration includes a final microsecond to make the last event reachable in a half-open range. No waveform is time-stretched: no audio exists in this milestone.

The block method is an offline oracle/worker interface that allocates Python objects, **not a realtime-safe audio callback**. A future native renderer must preallocate and admit scheduled actions independently. Block partition invariance holds when admission capacity is sufficient. Under overload the earliest events in each block are retained and omissions reported; changing block sizes can change which events survive an overloaded block. Offline composition must preselect/freeze actions before rendering if invariant overload behaviour is required.

Seeking requests another time window; the future audio consumer must invalidate old scheduling generations and reset voices/effects. Loop and live speed-change transport controllers, note durations, musical density transforms, and audio-tail reconstruction are not implemented. The API currently exposes prepared event plans, not a running audible transport.

## Portable local bundles

`session-export` writes a `.atlas.json` file with format version, frozen material, permitted metadata and integrity checks. Maximum file size is 64 MiB. Export publishes a fully flushed private file atomically and refuses to overwrite an existing path. No ZIP extraction, arbitrary file paths inside bundles, raw provider payloads or authentication tokens are supported.

`session-import` validates the format, checksums, bounded event counts, event field allowlist, times and coordinates. It creates a saved session in a separate library without importing anything into the live source/event tables. Consequently reopening a bundle cannot trigger wildlife arrivals. Checksums detect corruption, not prove provider authenticity; imported provenance remains explicit.

Distribution permission remains unresolved for the actual source data. These bundles are local preservation, not a declaration that the data may be shared publicly.

## CLI examples

Run these from the project directory if using Terminal directly:

```sh
python3 -B -m animal_atlas serve --config config/local.json --record-seconds 3600 --duration 3600
python3 -B -m animal_atlas record-start --name 'Evening collection' --seconds 7200
python3 -B -m animal_atlas record-stop SESSION_ID
python3 -B -m animal_atlas sessions
python3 -B -m animal_atlas snapshot --name 'Cached NZ birds' --source birdweather
python3 -B -m animal_atlas replay latest --mode observed --speed 60 --limit 5
python3 -B -m animal_atlas session-export latest 'Sessions/my-piece.atlas.json'
python3 -B -m animal_atlas session-import 'Sessions/my-piece.atlas.json'
```

`record-start`/`record-stop` require the serving collector/API. The library, snapshot, bundle, and replay commands work with the collector stopped and no network available. `replay` outputs a timing plan; it does not make sound. `latest` chooses the most recent frozen/interrupted session among the 100 listed sessions.

## Application API additions

All existing loopback authentication, host checks and browser-Origin denial still apply.

- `POST /v1/sessions/start` JSON `{name,sources?,seed?,duration_seconds?}`
- `POST /v1/sessions/SESSION_ID/stop` JSON `{"stop":true}`
- `POST /v1/sessions/snapshot` JSON `{name,sources?,seed?,observed_from?,observed_to?}`
- `GET /v1/sessions?limit=100`
- `GET /v1/sessions/SESSION_ID`
- `GET /v1/sessions/SESSION_ID/events?after_ordinal=-1&limit=200`
- `GET /v1/sessions/SESSION_ID/timeline?mode=observed&speed=60&after_index=-1&limit=200`

JSON POST bodies are at most 8 KiB. Read pages are at most 500 events. Replay planning occurs off the audio thread and currently rebuilds a bounded plan per request. A later application can cache an immutable plan by fingerprint. No HTTP endpoint writes arbitrary export paths.

## Validation

Automated tests cover immutable revisions, recording boundaries, baseline/correction exclusion, interrupted recovery, timed finalisation, filters, empty sessions, limits, corrupt bundles, raw-field exclusion, migration, portable restore, distinct late-upload timelines, acceleration, seeking, sample partitions and overflow. API tests exercise authenticated record/freeze/read/replay and request validation.

`tests/session_smoke.py` explicitly runs a 90-second free-source collection, freezes an 80-second event recording, saves a cached real-data snapshot, stops the server, and verifies both replay modes after restoring the bundle into a separate empty database. It is not run by unit-test discovery. Evidence distinguishes this short live check from a soak test, GUI test or audio test.
