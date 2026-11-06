# Animal Atlas backend

M1 is complete on `beta-1`: [verified native data pieces, historical windows and test evidence](docs/milestones/M1_VERIFIED_PIECE.md). [Build checkpoint log](docs/milestones/M1_BUILD_LOG.md). [Claude design handoff prompt](<Claude Design/CLAUDE_DESIGN_HANDOFF_PROMPT.md>).

The [revised build plan](docs/BUILD_PLAN.md) is the current product/engineering sequence. See the [consolidated Astra audit](docs/audits/AUDIT_AND_REVISED_BUILD_PLAN.md) for demonstrated issues, repaired items and release gates, and [AGENTS.md](AGENTS.md) for the active C++/realtime/agent rules. Standalone first with a shared C++ engine and later VST3 remains the recommendation; no audible engine or graphical product is implemented yet.

## Production direction and C++ migration

The production codebase is moving to portable C++20, with thin JUCE standalone/VST3 adapters. The proposed UI is React/TypeScript with a Three.js globe in JUCE's embedded browser. React and the webview are complementary. The C++ event/replay core and read-only native session CLI now build; collector/session writes remain the operational Python prototype below until parity and recovery testing support replacing it. Existing terminal commands and ongoing recordings are unchanged.

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native -j 4
ctest --test-dir build/native --output-on-failure
build/native/atlas-native sessions --db .state/atlas.sqlite3
build/native/atlas-native replay --db .state/atlas.sqlite3 --session latest --mode observed --speed 60 --limit 5
```

The C++ core needs no JUCE, React or Python runtime. CTest storage fixtures require Python 3.9+ at development time. The storage adapter uses SQLite; on this Mac select the already-installed Homebrew headers/library as documented in the Beta 1 notes, because the system runtime failed the closed-WAL fixture. See [native and React integration notes](docs/native-react-architecture.md) and the proposed [UI host contract](ui/contracts.ts). Windows/Linux, JUCE integration and webview rendering still need qualification.

## Operational Python prototype

Local wildlife collection and a real-time application import bridge. Runtime uses **Python 3.9+ standard library and SQLite only**: no paid services, accounts, API keys, hosted infrastructure, or package downloads are needed to run it. Run from source; the optional packaging metadata is not needed.

Version 0.3 adds sealed metadata, provenance-preserving bundles and full historical playback windows to recording, immutable saved sessions, observation/arrival replay plans, speed control and portable local bundles. This is a backend, not a synthesizer, atlas GUI, DAW plug-in or production service. Collection is periodic polling, not a provider push stream. Commercial source reuse is unresolved; local research access is not represented as permission to distribute a product or dataset.

## Open without entering commands

Open **Animal Atlas.app** or double-click **Start Animal Atlas.command** in this folder. It opens a numbered Terminal menu for recording, saved sessions, replay previews and export. This is a local launcher using the installed Python, not the finished graphical application. Shell syntax/app manifest are verified; Finder launching has not been manually exercised.

A real recorded session and a saved NZ bird snapshot are available in the local library, with a private portable bundle in `Sessions/`. See `evidence/session-milestone.json` and [recording/replay instructions](docs/sessions-and-replay.md). Those are historical milestone results; they do not describe the current state of an owner-started collector.

## Run

From Terminal:

```sh
cd '/Users/henryjones/Documents/ChatGPT/Generative Nature Synth'
python3 -B -m animal_atlas serve --config config/local.json
```

Leave that terminal running. In another terminal in the same directory:

```sh
python3 -B -m animal_atlas status
python3 -B -m animal_atlas events --limit 10
python3 -B -m animal_atlas watch
```

`watch` prints newly discovered eligible events as JSON lines. The initial historical baseline does not generate live arrivals. Ctrl-C stops the command. It does not install a background service, prevent sleep, or launch on login.

For a bounded run:

```sh
python3 -B -m animal_atlas serve --config config/local.json --duration 3600
```

Other useful commands:

```sh
python3 -B -m animal_atlas collect-once --config config/local.json
python3 -B -m animal_atlas serve --no-collect
python3 -B -m animal_atlas watch --after-seq 0 --include-baseline --batches 1
```

The first command makes one collection pass and exits. The second serves saved data without network requests. Use `--state-dir /absolute/path` **before** the subcommand to choose a separate local database. Defaults to `.state` relative to the working directory. Do not place the active database in a network/sync folder. A state-directory lock prevents duplicate collector processes.

## Implemented

- BirdWeather GraphQL detections; default mainland NZ bounding box `south=-48,west=166,north=-34,east=179.9`, including nearby sea. This does not include all NZ territories or the Chathams.
- Public NZ Great White App latest-position snapshots. Discovers bootstrap/config; stops if the approved public target changes. Does not fetch imagery, map tokens, private installations or whole track histories.
- Separate workers: bird polling at least 60 seconds apart; shark polling at least 15 minutes apart. Actual start-to-start interval includes fetch time and up to 3 seconds jitter. HTTP errors back off, including Retry-After.
- Canonical UTC times, coordinate validation, source namespaces, provenance, nullable identities and confidence semantics.
- Transactional SQLite persistence, immutable revisions and durable ordered change log. Corrections never become new live triggers. Raw source evidence is private to the local database, not returned through the API.
- First-run and changed-query-scope baseline suppression. An incomplete initial paginated fetch does not finish the baseline.
- Authenticated localhost API, long polling and independent consumer cursors. No unbounded subscriber queues.
- Source health, latest observation, pagination limits, invalid counts and known coverage limitations. Successful-poll ledger is saved in SQLite.
- Private state directory/token; request bodies, credentials and tokens are not logged. Network JSON responses are capped at 8 MiB, API batches at 500 events, and request concurrency at 12.
- Collection admission stops at a 512 MiB database-file soft limit or less than 64 MiB free disk. The soft limit is checked before a batch, so one transaction can cross it. Disk-full errors do not advance ingestion checkpoints. Automated retention is not yet implemented: archive/rotate the state deliberately.

## Verified and limited

See `evidence/live-smoke.json` for the original collection/subscription check and `evidence/session-milestone.json` for recording, freezing and offline restoration. Live data lives in ignored `.state/atlas.sqlite3`; synthetic unit fixtures are constructed in tests and never ingested as real data.

BirdWeather is a rolling observation-time window, not a proven publication feed. Current `InputDuration.from/to` schema fields are dates; this adapter uses the documented `count/unit` minute window. Pages are queried while the window moves. Pagination exhaustion does **not** prove every detection was collected. Late uploads outside the window and downtime longer than the window can be missed. `possible_unrecoverable_gap` is a sticky warning, not a comprehensive completeness detector. The API's reported `totalCount` was 0 while the initial small test returned real nodes, so this collector does not trust totalCount as a completeness check.

NZ shark collection currently records the latest fix for each provider subject. It can miss intervening fixes. Observation age may be days or months even when requests work. Provider subject ID is preserved separately: individual/deployment IDs remain null pending evidence-backed identity reconciliation. Same-name sharks are not automatically merged. There are no derived paths or speed estimates.

The shipped config is NZ-focused. Set BirdWeather `bbox` to null for global requests, but the 1,000-record poll cap may be reached repeatedly; this is **not a complete global ingestion solution**. Scoped collection/sharding and fuller history reconciliation are future work. Neither source has a verified commercial redistribution permission or availability guarantee. There is no fallback to a paid API or cloud service.

## Tests

```sh
python3 -B -m unittest discover -s tests -v
```

Includes real localhost sockets, but no external API calls. Integration tests require permission to bind loopback. Synthetic tests cover restart/deduplication, correction/reversion history, atomic rollback, baseline suppression, new arrivals, pagination, malformed records, timezone/coordinates, 429 handling, endpoint allowlisting, authentication and multi-consumer delivery.

Explicit bounded external-network smoke test (uses `.state`, requires no collector already running):

```sh
python3 -B tests/live_smoke.py
```

It runs the collector/API for 90 seconds, checks real deliveries through two clients, writes summary evidence, and stops. This is not a 48–72 hour soak test. No GUI or audio callback is attached yet.

See `docs/protocol.md`, `docs/sessions-and-replay.md`, `docs/architecture.md`, and `docs/source-registry.json` for integration and remaining work.
