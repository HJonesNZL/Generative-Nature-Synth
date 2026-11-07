# Live collection and scalable libraries — 7 November 2026

Implemented on `beta-1` with GPT-6.1 SOL agents for portable collection, storage/service and UI/test work, plus parent integration, review and packaged qualification. Implementation checkpoint: `a894971`. This ledger accompanies the final startup-display fix and release evidence.

## What changed

The prior running Project Recovery app had no library attached and no active collector. This release adds native, explicit in-app collection into a separate managed `live.sqlite3`. BirdWeather acoustic station detections and NZ shark source-subject location snapshots retain distinct semantics. The owner’s existing data, settings, processes, design archives and older release bundles were not modified.

Collect starts selected sources, refreshes into the managed library and leaves audio disabled. Every launch starts collection paused. Genuine fresh later arrivals can ping; first snapshots, restarted backlogs, corrections and older backfills do not. Partial bird coverage stays explicit. Pausing an in-flight request discards buffered data without inventing a source fault; actual provider errors and retry limits remain. Old-document requests cannot mutate a newly selected library. Choosing an external library pauses the native collector and uses read-only browsing.

Saved-piece browsing now uses revision-fenced cursor pages, preserves selection and reaches thousands of pieces without an unbounded UI list. One verified material cache removes repeated preparation I/O and is invalidated on external database changes. Mixed-revision event pages clear the partial display and require retry. Map baselines retain quieter positioned fixes alongside recent detections. Grain now displays actual report species before the catalogue refresh arrives; no missing data is synthesized.

## Artifact and use

Use `Releases/Live Library/Animal Atlas.app`, or the adjacent `Animal-Atlas-Live-Library-arm64.zip`. Close the earlier app first, open this version, then choose **Collect → Start live updates**. No Terminal/Node/Vite/Python process or paid service is required at runtime. The local arm64/macOS 15+ package contains production UI, fonts, maps, engine, SQLite and notices. Its signature is local ad-hoc, not public Developer ID/notarisation.

Native source SHA-256: `2069c091a93633f63f5860f370155d2b08e1d3cf9bcd68e641bc926b48fa5cf9`. ZIP SHA-256: `2fe8fddd49368dcafee71d3859364faea16bac97a90f4229ec3fc573c09964f4`. The [artifact manifest](artifact.json) records exact binary, resource and dependency hashes. Local generated bundles remain ignored by git; source, reproducible packaging and evidence are committed.

## Executed checks

| Check | Result and scope |
| --- | --- |
| Native Release | All **30/30 CTest suites** pass with project warnings as errors. Includes collection, service, paging, map-source coverage, large-library protocol, existing playback/voices/scenes/rendering/projects and lifecycle tests. [Log](native-release-tests.log). |
| Portable sanitizers | **5/5 ASan/UBSan suites** pass for collection, library browsing, UiHost, map-source coverage and the 20,000-event library protocol. [Log](native-sanitizer-tests.log). |
| Service concurrency | Strict warning build plus ASan/UBSan and TSan service tests pass; injected HTTP exercises pause/restart/retry/cancellation and serial worker ownership. Project sources instrumented; prebuilt JUCE/system SQLite were not. No transport claim inferred. [Receipts](service-sanitizers.json), [exact local build helper](service-sanitizer-build.py). |
| UI boundaries | TypeScript passes; **57/57 unit tests** pass. Request identity, page size, bounded cursors, known source IDs, source mutation acknowledgements and status payloads are checked. [Log](ui-unit-tests.log). |
| Browser paging | Traversed **2,607 pieces**, retained selection, rejected stale responses and checked Atlas/Studio at 1440×900 and 1280×800. [Receipt](session-browser.json). |
| Live controls | Injected host verifies start/apply/pause, source choices, actual page reload, scene-only versus archive browsing, freshness, partial coverage, errors, retries and disclosure fit. [Receipt](live-source-browser.json). |
| Stale material | First page displayed, stale second page cleared events/counts/map markers, visible failure and explicit fresh retry preserved selected preparation. [Receipt](material-revision-browser.json). |
| Catalogue lag | Real report lanes/names appear with an empty catalogue; six-lane cap, identity/name fallbacks, known catalogue order, highlighting and screen continuity pass. [Receipt](grain-catalog-lag-browser.json). |
| Build isolation | Production/demo bundles pass separation, CSP, local notices/assets and lazy globe checks; production contains no development transport. |
| Actual packaged live UI | Real WKWebView Collect control → managed library/epoch handoff → JUCE HTTPS → SQLite → map/species/Grain, followed by a second bird poll and pause, passes in disposable state. **428 baseline records, zero baseline pings; 68 later records including 43 eligible new pings.** React displayed **496 records, 68 locations and 21 species rows**, with a globe canvas. Audio stayed disabled. [Receipt](packaged-live-smoke.json). |
| Extracted release ZIP | Strict signature verification and launch outside the repository; independent scene import/recall, native stereo WAV render/save, exact scene export, project backup/restore and silent device catalogue checks. [Receipt](installed-zip-smoke.json). |
| App lifecycle | Missing library/preferences retained across restart, invalid settings retained, concurrent state writer rejected, lock released on exit. No provider contact in these checks. [Receipt](installed-zip-lifecycle.json). |

Desktop reference screenshots use injected fixtures, **not actual wildlife evidence**: [Atlas](atlas-1440x900.png), [expanded Collect at 1280](collect-1280x800.png), [Studio](studio-1440x900.png). The actual native receipt supplies the live-data assertions.

## Actual provider observations

Final native network smoke lasted 70.52 seconds in fresh disposable state. Its end-of-second-poll status was:

- birdweather: last successful fetch `2026-11-07T02:21:13.000000Z`, newest observation `2026-11-07T02:20:55.000000Z`, 468 stored records, complete=false.
- nzsharks: last successful fetch `2026-11-07T02:20:13.000000Z`, newest observation `2026-11-06T18:58:18.000000Z`, 28 stored records, complete=true.

Bird coverage was explicitly partial (`pagination_limit_reached`), with four 100-record pages per check. Partial coverage does not mean no fresh data and does not assert full source completeness. The latest shark fix was several hours older than its fetch. The app did not label it a current position. These point-in-time checks are not a long-running soak or a guarantee of provider availability.

## Named preparation benchmark

[Full before/after record](preparation-benchmark.json): `LargeLibrary-20000-events-3003-pieces-v1`, 20,000 synthetic mixed station/fix events, 3,003 sealed pieces, 61,214,720-byte database, three fresh-host runs per binary.

| Median | Previous binary | New preparation implementation |
| --- | ---: | ---: |
| Catalog → preparation | 1362.35 ms | 729.53 ms |
| Warm preparation | 644.51 ms | 8.26 ms |
| Warm 500-event page | 23.55 ms | 23.12 ms |
| Peak process RSS | 118.44 MB | 114.48 MB |

The measured gain comes from avoiding repeat verification/JSON loading when preparing unchanged material. Page serialization remains essentially unchanged. The benchmark records earlier comparison binary hashes; final integration was separately rechecked with the [large-library protocol](library-protocol.json). OS file caches were not flushed. These values do not qualify arbitrary library sizes, audio callback load or human listening.

## Limits and next milestone

These streams are **bird acoustic activity and shark location snapshots**, not continuous identified-animal tracks. The latest verified Movebank/ATN probes were historical; no third live feed is advertised. Access, freshness, identity, location accuracy and reuse permission remain separate. [Source research and contract](../../docs/data/LIVE_SOURCE_MILESTONE.md).

Collection uses bounded polling while the app is open. The managed DB stops collecting with an explicit capacity/storage error at 512 MiB or low disk; it never silently deletes observations. Display retains at most 2,000 events / 500 markers, and current recipe admission remains 50,000 events. Native freezing/capture and historical import into that new library are not yet implemented. Existing saved libraries and self-contained soundscapes remain usable. Project backups exclude raw wildlife libraries; copy/export those separately when needed.

Live sonification, human listening, physical hotplug/sleep/wake, sustained provider soak, real full-disk/power-loss behavior, another-machine qualification, public distribution rights/signing and VST3/DAW hosting remain separate work. No physical output was enabled by these new integration checks.

Next: **native capture/freeze and bounded, provenance-preserving historical import**, including resumable progress and an evidenced identified-animal archive source. See the [completed scope and next plan](../../docs/milestones/NEXT_LIBRARY_BROWSING.md) and [quick start](../../docs/STANDALONE.md).
