# Animal Atlas

Open **Releases/Saved Wildlife Pieces/Animal Atlas.app**, choose **Collect**, and select **Start live updates**. Worldwide bird detections and NZ shark reports are selected initially. Enable **Worldwide reported sightings** to add public iNaturalist animal reports. Choose **NZ bird detections** for deeper local coverage. **Latest detections** locates a report on the globe. [Standalone guide](docs/STANDALONE.md) · [Worldwide qualification](evidence/worldwide/README.md) · [Identified spotted seal archive](deliverables/ATN-spotted-seal-2018/README.md)

In **Project → Saved pieces**, freeze a UTC window from collected records or import a sealed piece from an attached archive. Open the result in Replay, edit its voices in Sound Studio, then save the soundscape and export a WAV. The bundled seal archive works offline. [Saved pieces and replay](docs/STANDALONE.md#save-a-wildlife-piece)

The [live library milestone](docs/milestones/NEXT_LIBRARY_BROWSING.md) added source freshness, paginated saved-piece browsing and revision-aware preparation. [Qualification record](evidence/library-live/qualification.md)

The [project recovery milestone](evidence/project-recovery/qualification.md) adds draggable NZ detail, portable scene export, verified project backup/restore and WAV history that survives restarts. Its earlier package remains under **Releases/Project Recovery/**; see the [quick start](docs/STANDALONE.md). All project operations remain local and free of paid services.

The [standalone beta milestone](docs/milestones/STANDALONE_PLAN.md) packages the native instrument and local React interface in a macOS app, with output-device selection, scene import and native WAV saving. See the [standalone quick start](docs/STANDALONE.md) and [qualification evidence](evidence/standalone/qualification.md). This local arm64 beta requires macOS 15 or later and needs no Node, web server or repository at runtime.

The [composition engineering checkpoint](evidence/composition/qualification.md) adds deterministic species phrases, key/scale/tempo/density/variation/seed controls, full scene recall and legacy compatibility. See [composition controls](docs/milestones/COMPOSITION_UI.md) and the completed [regional mixing checkpoint](evidence/spatial/qualification.md). Sound Studio now edits up to eight geographic sound regions with stereo placement and echo/ambience; scenes preserve all controls and export through the same native engine.

M1 is complete on `beta-1`: [verified native data pieces, historical windows and test evidence](docs/milestones/M1_VERIFIED_PIECE.md). The [M2 first-sound engineering slice](docs/milestones/M2_FIRST_SOUND.md) implements native offline synthesis/WAV export and now has [experimental native saved-piece playback in the UI](docs/milestones/UI_NATIVE_PLAYBACK.md). Sound Studio supports [native per-species voice editing](docs/milestones/UI_NATIVE_VOICES.md), and now [complete soundscape recall, asynchronous WAV export and researched candidate presets](docs/milestones/SOUNDSCAPES_AND_SPECIES_CANDIDATES.md). The [playback contract](docs/milestones/NATIVE_PLAYBACK_CONTRACT.md) defines ownership, controls and failures. Human listening and whole-M2 acceptance remain open. [Build checkpoint log](docs/milestones/M1_BUILD_LOG.md). [Claude design handoff prompt](<Claude Design/CLAUDE_DESIGN_HANDOFF_PROMPT.md>).

The [revised build plan](docs/BUILD_PLAN.md) is the current product/engineering sequence. See the [consolidated Astra audit](docs/audits/AUDIT_AND_REVISED_BUILD_PLAN.md) for demonstrated issues, repaired items and release gates, and [AGENTS.md](AGENTS.md) for the active C++/realtime/agent rules. Standalone first with a shared C++ engine and later VST3 remains the recommendation. The H2 graphical UI can control the optional native audio host through `getTransport`, `setTransport`, `getAudioState` and `setAudioEnabled`; the data-only host and separate illustrative demo remain available. The packaged app embeds the interface and native engine; the separate command-line host remains available for development. The plug-in and physical device recovery/listening qualification remain open. [Frontend integration readiness](docs/milestones/FRONTEND_INTEGRATION_READINESS.md) records the earlier data-only checkpoint; the linked playback milestone supersedes its audio-availability statements for the optional audio host.

Open **Start Atlas UI.command** for the two-screen interface. It prefers a built optional audio host and otherwise retains the data-only fallback. See [UI launch and integration instructions](ui/app/README.md), [saved-piece playback](docs/milestones/UI_NATIVE_PLAYBACK.md), [voice editing and saving](docs/milestones/UI_NATIVE_VOICES.md), and [earlier integration scope](docs/milestones/UI_HANDOFF_INTEGRATION.md). Existing collection commands remain separate.

Playback requires explicit Listening enable. Eight native voice controls and six experimental synthesis characters affect new notes. Explicit Save voice persists independent definitions in `.state/voices.sqlite3`; complete soundscapes copy events, time settings, global settings and exact species recipes into `.state/soundscapes`. The UI exports immutable saved scenes to 24-bit WAV in `Exports`, independently of later draft edits or the original wildlife database. New unsaved species use a researched candidate bank with explicit taxonomy/evidence limits; existing saved voices are preserved. See [broader sound-design research](docs/sound-design/RESEARCH_AND_DESIGN_PRACTICE.md). Phrase generation, regional effects and embedded webview packaging are implemented. Live sonification, runtime master editing and DAW delivery remain future work.

## Production direction and C++ migration

The production codebase is moving to portable C++20, with thin JUCE standalone/VST3 adapters. The current UI is React/TypeScript with a Three.js globe; JUCE's embedded browser is the local macOS product container. React and the webview are complementary. The C++ event/replay core, numeric action compiler, bounded voice renderer, WAV writer, playback runtime and read-only native session CLI now build; the packaged app now collects into its separate managed native live library. The native app freezes and imports saved pieces into its managed database. The operational Python prototype below retains its own recording database. Existing terminal commands and ongoing recordings are unchanged.

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native -j 4
ctest --test-dir build/native --output-on-failure
build/native/atlas-native sessions --db .state/atlas.sqlite3
build/native/atlas-native replay --db .state/atlas.sqlite3 --session latest --mode observed --speed 60 --limit 5
```

The C++ core needs no JUCE, React or Python runtime. CTest storage fixtures require Python 3.9+ at development time. The storage adapter uses SQLite; on this Mac select the already-installed Homebrew headers/library as documented in the Beta 1 notes, because the system runtime failed the closed-WAL fixture. See [native and React integration notes](docs/native-react-architecture.md) and the shared [UI host contract](ui/contracts.ts). Historical playback-only macOS Debug and ASan+UBSan runs passed eight CTest suites each; the optional Release audio-host build passes nine suites, including 13 runtime groups and nine playback-protocol groups; an independent ThreadSanitizer run passes all 13 runtime groups. See [playback evidence](evidence/native-playback/) and the historical offline checkpoint in `evidence/m2-audio/final-*`. The ninth suite exercises real-JUCE preparation/shutdown without enabling output. A [browser-to-device smoke](evidence/native-playback/ui-device-smoke.json) also passed through the complete UI and native output path. A [brief actual default-output callback smoke](evidence/native-playback/device-smoke.json) passed at 48 kHz and -18 dB: enable remained silent until Play, native position advanced, pause/seek/resume/stop worked, and EOF exited cleanly. This does not qualify listening, device changes/recovery, sustained operation or callback deadlines. Human listening, whole-product resource qualification and Windows/Linux remain open; the newer standalone evidence covers local packaging and actual WebKit rendering. Synthetic process timing does not prove physical listening quality.

## Operational Python prototype

Local wildlife collection and a real-time application import bridge. Runtime uses **Python 3.9+ standard library and SQLite only**: no paid services, accounts, API keys, hosted infrastructure, or package downloads are needed to run it. Run from source; the optional packaging metadata is not needed.

Version 0.3 adds sealed metadata, provenance-preserving bundles and full historical playback windows to recording, immutable saved sessions, observation/arrival replay plans, speed control and portable local bundles. This Python component remains a backend prototype; native synthesis and the React interface use a separate native host boundary, with no DAW plug-in or production-service qualification. Collection is periodic polling, not a provider push stream. Commercial source reuse is unresolved; local research access is not represented as permission to distribute a product or dataset.

## Open without entering commands

For the older data-collection menu, open the root-level **Animal Atlas.app** or double-click **Start Animal Atlas.command** in this folder. The current graphical app is under **Releases/Worldwide Wildlife/**. It opens a numbered Terminal menu for recording, saved sessions, replay previews and export. This is a local launcher using the installed Python, not the finished graphical application. Shell syntax/app manifest are verified; Finder launching has not been manually exercised.

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

The live listening/detection stream is retained. Source expansion prioritises genuine individual-animal locations with evidenced animal/deployment identities; generic GBIF occurrences are optional/deferred. The [offline data pilots and review](data-pilots/REVIEW.md) prepare contracts only: no Movebank/GBIF source or proposed reliability patch is active. The full synthesis palette, modulation, species/individual mappings, regional effects and historical composition remain product targets; the first tone and eight-voice experiment are not product ceilings.

## Tests

```sh
python3 -B -m unittest discover -s tests -v
```

Includes real localhost sockets, but no external API calls. Integration tests require permission to bind loopback. Synthetic tests cover restart/deduplication, correction/reversion history, atomic rollback, baseline suppression, new arrivals, pagination, malformed records, timezone/coordinates, 429 handling, endpoint allowlisting, authentication and multi-consumer delivery.

Explicit bounded external-network smoke test (uses `.state`, requires no collector already running):

```sh
python3 -B tests/live_smoke.py
```

It runs the collector/API for 90 seconds, checks real deliveries through two clients, writes summary evidence, and stops. This historical smoke predates the current UI/native playback integration and is not a 48–72 hour soak or an audio/device test.

See `docs/protocol.md`, `docs/sessions-and-replay.md`, `docs/architecture.md`, and `docs/source-registry.json` for integration and remaining work.

## Soundscapes and auditioning candidates

Current milestone: [implementation, validation and limits](docs/milestones/SOUNDSCAPES_AND_SPECIES_CANDIDATES.md). Native final validation passes 16 Debug suites, 16 ASan+UBSan suites and 17 Release suites; the latter includes real JUCE startup without enabling hardware output. These supersede the older suite counts above for this code revision.

The locally generated `Auditions/species-candidates-v1/index.html` offers 33 synthetic comparison files. It is intentionally untracked along with WAV exports and owner soundscapes. To regenerate into a **new** directory:

```sh
build/native-playback/atlas-native audition-presets --output Auditions/another-comparison
```

To render a complete saved scene without the original wildlife database:

```sh
build/native-playback/atlas-native scene-render --scene .state/soundscapes/SCENE_ID.atlas-scene --output Exports/new-render.wav
```

Commands refuse existing output files. Reference animal recordings are linked in the research dossiers; no recordings were copied into this project or auditioned by the agent.
