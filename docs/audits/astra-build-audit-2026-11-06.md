# Animal Atlas implementation audit — 6 November 2026

Independent audit of the Python collection/session prototype, the portable C++ replay milestone, and the selected H Biosphere Console product plan. This is a baseline audit, not a statement that subsequent repairs are included. The accompanying `astra-build-audit-manifest-2026-11-06.json` identifies the inspected files.

## Decision

**Continue development; do not replace the running collector or describe this as a working instrument yet.** The native core is a useful, genuinely framework-independent scheduling milestone. The selected UI architecture is defensible. No P0 defect was established. One reproducible recording-recovery defect deserves immediate repair; session integrity and provenance must be completed before native replay becomes the authoritative production path.

The first audible slice is still absent. There is no audio processor, mapping/admission engine, voice allocator, effect implementation, device lifecycle, native service, JUCE target, React application, webview bridge, historical species/region query, WAV renderer, or VST3 target in the inspected source. The `.app` launches a Terminal menu. Design images and interface declarations do not establish those features.

**Remediation status at handoff:** the parent agent elected to repair A1 and unnecessary comparator string copies in `native/src/core.cpp:134–135`. Those repairs are pending the parent's verification and are not included in the baseline test results below. The integrity, schema and historical-provenance findings remain explicit gates.

## Scope and evidence

Read the original Downloads handoff, README, all existing `docs/*` documents and source registry, selected-direction JSON files, the full Direction H feature brief, `ui/contracts.ts`, every Python implementation module, native sources/headers/tests, CMake, Python tests, launch scripts, and recorded evidence. The copied source registry was checked against the repository copy. Visual mockup quality was outside this audit; another agent owns UI design.

Used the Field Audio Systems audit workflow. The owner database, tokens, live commands, collector, watcher, and session state were not inspected or mutated. No provider requests were made. Isolated fixtures and builds used temporary directories. No product source was changed by this audit.

Fresh checks:

| Check | Result | Limit |
|---|---|---|
| `python3 -B -m unittest discover -s tests -v` | 49 passed | Initial sandbox run had seven loopback bind errors; rerun with auto-approved loopback access passed. All stores were temporary. |
| Release configure/build under `/private/tmp/animal-atlas-astra-audit-build` | Passed, AppleClang 17, arm64, SDK SQLite 3.51.0 | Does not establish Windows/Linux compatibility. |
| Release CTest / native executable | 16 native cases passed | Only `atlas_core` is registered in CTest; native storage lacks a self-contained CTest fixture suite. |
| Fresh isolated counterexamples below | Reproduced | Synthetic, not owner data. |
| Existing native parity artifacts | 2 frozen sessions, 16 comparisons, 18,992 event comparisons recorded | Historical evidence inspected; this audit did not rerun a test against the owner library. |

Build command:

```sh
cmake -S '/Users/henryjones/Documents/ChatGPT/Generative Nature Synth' -B /private/tmp/animal-atlas-astra-audit-build -DCMAKE_BUILD_TYPE=Release -DATLAS_BUILD_TESTS=ON
cmake --build /private/tmp/animal-atlas-astra-audit-build -j 4
ctest --test-dir /private/tmp/animal-atlas-astra-audit-build --output-on-failure
```

Exact isolated probes for A1–A5 have been preserved at `/private/tmp/atlas-astra-audit-reproduce.py` and can be rerun with `python3 -B /private/tmp/atlas-astra-audit-reproduce.py`. They construct temporary stores, patch only the fixture collector's network poll, use the Release native binary above, print observed results, and remove their temporary databases. The probe script does not assert the defects must remain: a repaired A1 should report `interrupted` immediately after `collect-once` and only `before-crash` in the old recording. A2–A5 outputs described below are the baseline observations. These scripts are audit artifacts in temporary storage, not product source.

## Prioritized findings

### A1 — P1: `collect-once` contaminates an interrupted recording with later acquisition

**Evidence:** `animal_atlas/__main__.py:145–155`; `animal_atlas/sessions.py:118–135,196–200`. The `collect-once` branch acquires writer ownership, polls, and returns before `sessions.recover()`. Recovery is performed only on the `serve` branch.

**Trigger and consequence:** a serving process dies while recording; the owner later runs `collect-once`; a later server recovery freezes every eligible revision after the old start sequence, including the new command's material. The old interrupted recording gains events acquired after its process was gone. This defeats the intended defensible interruption boundary.

**Reproduction:** in a temporary store, ingested a baseline, started a recording, ingested `before-crash`, then invoked the actual `collect-once` CLI path with `Collector.poll` replaced by a fixture that ingested `after-crash-new-command`. No provider requests. The old session remained `recording`; subsequent recovery contained both IDs.

**Smallest repair:** recover previous active sessions under the exclusive collector lock before *any* acquisition branch, including `collect-once`, and before starting a new recording. Keep the existing interruption marker.

**Regression gate:** crash/abandoned recording → `collect-once` → restart; the recovered session contains only pre-handover events, stays interrupted, and repeated recovery is idempotent. Also test failed startup and no-collection service startup.

### A2 — P1 production gate: native replay accepts changed session material without verifying its saved hash

**Evidence:** `native/src/storage.cpp:50–57,112–157` never selects or verifies `content_hash`; `native/src/main.cpp:62` discloses the omission. Python checks it at `animal_atlas/replay.py:30–32`.

**Trigger and consequence:** a stored event changes while count, ordinal, time, and coordinates remain structurally valid. Native replay treats the changed composition as the saved piece. This is an acknowledged incomplete migration, not a concealed implementation claim, but it blocks authoritative native recall/render.

**Reproduction:** changed one temporary frozen event's latitude from −41 to −42 without changing its saved hash. Python rejected it with `session integrity failure`; the native CLI exited zero and emitted −42.

**Smallest repair:** verify the existing event-content hash before publishing a native prepared timeline. Specify cross-language canonical bytes, including Unicode, float formatting and sequence IDs; do not casually substitute another serializer's output. Hashing belongs on the worker, not in `Timeline::block`.

**Regression gate:** altered payload, sequence, ordering, count, Unicode, numeric edge cases, and valid portable bundles produce the same acceptance/rejection in both readers. Use temporary self-contained fixtures.

### A3 — P2: portable import discards historical selection and origin metadata

**Evidence:** `animal_atlas/sessions.py:249–258,268–270`. Import rebuilds options from the current `options()` factory, copies only bounds and truncation, replaces selection policy, resets creation/end timestamps, and supplies the current software version.

**Trigger and consequence:** exporting a range-filtered historical session then importing it preserves event timing but loses `observed_from`/`observed_to`, original selection policy/version, and interruption context. A future history UI cannot explain the original requested interval or how the material was acquired from that restored piece.

**Reproduction:** a snapshot requested 00:00–04:00 UTC. After export/import, both requested boundaries were absent; policy changed from `latest_event_revisions_at_snapshot` to `imported_frozen_timeline`.

**Smallest repair:** preserve a validated, versioned original selection/provenance envelope. Store import ID/time separately. Retain original requested range, timezone, policy/version, truncation and interruption metadata; extend the envelope to coverage/resolution and regions when implemented. Do not blindly retain arbitrary unvalidated bundle keys.

**Regression gate:** compare complete selection/provenance through two exports and two imports, including empty/partial historical selections and interrupted recordings. Existing roundtrip tests compare replay plans, so they miss this loss.

### A4 — P2: Python and native readers disagree on event semantics

**Evidence:** `animal_atlas/sessions.py:28–37` revalidates only identity/time/kind/location/coordinates and returns the original input. It does not pass optional identity fields to validation. `native/src/core.cpp:85–92` rejects a station acoustic detection with an individual ID. Native SQLite string extraction also does not consistently require JSON text types (`native/src/storage.cpp:31–39,138–141`).

**Trigger and consequence:** a correctly checksummed bundle can import/replay in Python with an invented individual identity, then fail native migration. Other optional fields lack a shared typed semantic contract. A checksum cannot establish biological validity or provider authenticity.

**Reproduction:** added `individual_id="invented-individual"` to a station acoustic detection, recomputed both public checksums, and imported it. Python accepted and replayed it; native rejected it as an invented station individual.

**Smallest repair:** define one versioned event schema plus explicit cross-field invariants and implement the same validation on both ingress paths. Validate optional IDs/types, event/location-kind pairing, confidence representation and meaning, uncertainty, provenance enum, and namespaced identity. Preserve unknown as null.

**Regression gate:** a shared positive/negative fixture corpus must yield identical decisions in Python and C++. Include missing/null versus wrong-type fields, station versus animal identities, uncertainty and non-finite numeric inputs.

### A5 — P2: stored timing metadata is outside the integrity boundary

**Evidence:** `animal_atlas/sessions.py:40–44,110–116` hashes frozen rows only; `animal_atlas/replay.py:31–48` verifies that hash but trusts `timeline_bounds` and seed. The exported outer bundle hash covers metadata in transit, but the stored library has no equivalent envelope check.

**Trigger and consequence:** changed timing bounds or interpretation settings alter a piece while the saved event hash still validates. This is primarily accidental-corruption/recall protection, not authentication against the database owner.

**Reproduction:** after restoring unchanged event material, altered only the stored observed bounds. Python accepted the same event hash while the first event moved from due time zero to 7,200,000,000 microseconds.

**Smallest repair:** add a versioned session-envelope hash covering schedule-affecting and provenance metadata. Keep the existing content hash for compatibility; distinguish legacy unverified envelopes. Include mappings, regions, DSP versions and performance controls as those are introduced.

**Regression gate:** changing a bound, seed, query, mapping or region must invalidate the envelope; a supported migration must regenerate it explicitly. Reopen unchanged sessions across implementations.

### A6 — P2: replay entry points omit the saved capture-truncation warning

**Evidence:** truncation is stored at `animal_atlas/sessions.py:84–95`. Python `Timeline.summary()` (`replay.py:57–62`) and native replay JSON (`native/src/main.cpp:59–62`) omit it. Native session listing and the proposed `SessionView` do expose it.

**Consequence:** a caller using the replay response alone, including the existing preview CLI, sees a normal event count and duration for a truncated session. The stored flag is useful only if every caller performs another query.

**Reproduction:** froze a three-event fixture with capacity patched to two. Session metadata reported truncation, but its timeline summary contained no truncation field.

**Smallest repair:** include capture completeness/truncation in every prepare/replay response and carry it to rendering/export. Keep capture truncation distinct from musical grouping/omission and source coverage.

**Regression gate:** capped snapshot → Python/native replay/prepare → UI/render summary retains the warning at every boundary.

### A7 — P2 build gate: native compatibility tests are coupled to the owner's library and are not in CTest

**Evidence:** `CMakeLists.txt:37–41` registers only core tests. `tests/native_compatibility.py:21–35,39–42,76–77` hardcodes `.state/atlas.sqlite3`, requires frozen owner sessions and overwrites an evidence file. The test is explicitly read-only regarding the database, which is good, but it cannot qualify a clean checkout unattended.

**Smallest repair:** add a self-contained native storage/CLI suite using generated temporary schema-v2 fixtures, and make optional real-library parity accept an explicit DB path/output path. Separate immutable historical evidence from new validation output.

**Regression gate:** fresh checkout with no `.state` passes normal Debug/Release CI; storage tests cover malformed JSON/type fields, count/ordinal/hash failures, empty sessions, unsupported versions and read-only open. Run ASan/UBSan and Windows/Linux builds as separate platform gates.

## Data and historical availability assessment

The implemented acquisition is useful but narrow: BirdWeather rolling observation windows and NZ shark last-position snapshots. Baseline suppression, durable ordered revisions, source namespaces, atomic ingestion, correction handling, private raw data, independent cursors, and provider-specific confidence descriptions are real code. Neither adapter establishes a complete historical feed. Shark provider-subject IDs are correctly kept separate from evidenced individual/deployment identities.

`sources.py:106–154` uses moving BirdWeather windows, bounded pages and no publication cursor. `sources.py:191–217` extracts only the latest shark point. `sessions.py:138–162` supports local source/time snapshots but has no species, region or coverage query. The registry contains 30 candidates, only two marked implemented; all redistribution statuses remain unresolved. Keep these facts visible in capabilities.

Historical species + region + time is an accepted requirement, so it needs its own early delivery gate. First support a query over **local frozen/collected material**, showing requested range, actual material extent, unknown coverage, source precision and provenance. Introduce a source/study capability record and a persistent coverage ledger before adding provider archive discovery. Successful pagination proves only the fetch ended, and no events in a bin does not prove animal inactivity. The current successful-poll ledger and sticky gap flag cannot substantiate `confirmed_coverage_no_detections` for these sources.

The observed timeline deliberately trims to first/last selected events (`sessions.py:101–109`; documented in `docs/sessions-and-replay.md:43`). One event in a requested four-hour interval currently produces a one-microsecond timeline. This is documented prototype behavior, not a newly discovered defect. For History, choose and store an explicit trim-to-material versus preserve-requested-window policy; never present either as verified ecological silence.

Preserve distinct quantities: requested interval, accessible/source-covered interval, interval containing selected records, and playback/render interval. Record uncertainty, timezone/DST resolution, query version, source/study identity and terms evidence. Preserve original event provenance even when a session is imported, while separately marking its import lineage.

Before unattended multi-day collection, add consistent backup/restore, storage policy, and failure/coverage history. The 512 MiB admission check (`store.py:88–92`) applies to ingestion, not all session writes; saved snapshots duplicate event material and no pruning exists. Test low disk, failed finalization, slow provider reads, process termination and recovery. Shutdown joins each worker for a fixed timeout without verifying it stopped (`__main__.py:187–190`); require a bounded total fetch deadline and a proved quiescent handover before replacing this lifecycle in C++.

## Native scheduling and CPU assessment

The core is an appropriate foundation. Integer/rational timing is explicit; construction validates/sorts off-thread; `Timeline::block` performs two binary searches and at most the provided output capacity of copies. It does not parse JSON, access SQLite, allocate containers, lock, log or throw. For the admitted 50,000 events and at most 4,096 output slots, work is approximately O(log N + K). This is materially better than scanning the whole timeline per audio block.

A fresh **scheduler-only** Release microbenchmark used 50,000 simultaneous synthetic events, 64-frame calls at 48 kHz, 1,000 warmups and 10,000 timed calls on this Mac:

| Output capacity | Median | p99 | Maximum observed | Omitted from burst |
|---|---:|---:|---:|---:|
| 256 | 0.500 µs | 0.584 µs | 14.625 µs | 49,744 |
| 4,096 | 4.583 µs | 6.208 µs | 21.166 µs | 45,904 |

Preparation took 3.29 ms in this synthetic short-ID fixture. Benchmark source: `/private/tmp/atlas-astra-scheduler-bench.cpp`. These are timing observations of lookup only, not audio deadline qualification, system-wide CPU, worst-case bounds, or DSP capacity. There is no audio callback to profile yet. Timer resolution and OS scheduling affect the measurements.

Keep the documented overflow caveat: retained actions depend on block partition under overload (`native/src/core.cpp:175–183`). Do not use this scheduler's first-K-per-block rule as the final musical admission policy. Freeze deterministic selection/grouping before render, then guarantee note release/panic independently of note-on admission.

Worker-side efficiency also matters. The HTTP timeline path rebuilds, hashes, sorts and recomputes density for the entire session on every page (`api.py:157–159`, `replay.py:30–58`). At 50,000 events and 500 per page, reading the full plan can repeat that work 100 times; 12 concurrent requests multiply temporary memory. Cache bounded immutable preparations by content/settings fingerprint or serve prepared pages. Enforce aggregate memory limits, cancellation and cache eviction rather than relying only on a per-request event limit.

The baseline native comparator constructs value tuples containing event-ID strings (`core.cpp:134–135`). These copy strings per comparison and may allocate for longer real source IDs. Replace those with reference comparisons or compare time, then sequence, then ID without constructing owning strings. This is a worker-side preparation efficiency issue, not an audio callback defect; the short IDs in the microbenchmark do not expose its full allocation cost. Parent repair/verification is pending at this audit handoff.

## Proposed DSP/effects and architecture

There is no actual DSP design to approve or reject yet. The voice/effect names in the full brief (`147–188`) are creative proposals. A few contrasting voices plus modest delay/reverb is an appropriate initial scope; a broad synth/effect suite would postpone the product's first meaningful proof.

Implement a portable worker-side mapping/admission stage producing fixed-size musical actions: stable action/event IDs, sample frame, voice/parameter IDs, note lifetime, generation and source references. A frozen musical action manifest must include selection/grouping reasons, mapping version and seed. Data arrival time alone does not capture the original audible performance: live scheduling delays, dropped/grouped actions and parameter gestures must be recorded if faithful performance replay is promised.

Start with one stereo reference voice, a fixed voice pool, deterministic stealing, bounded envelopes, parameter smoothing, finite-value guards and a safe master. Decide note duration versus replay speed explicitly. Then add one complementary voice, one delay and one reverb with stated parameter bounds, maximum memory and tail/reset behavior. Preallocate from sample-rate limits and move resizing/filter redesign/asset loading off the callback. Evaluate oversampling only where an identified nonlinear or oscillator aliasing problem requires it.

For geographic effects, compute point/uncertainty/region membership on a worker. Freeze routing with the piece. Prefer a small capped set of shared regional buses; a reverb instance for every animal, detection or arbitrary polygon does not scale. Cap active buses and blends, define overlap priority and missing-location behavior, and define whether old tails stay with their original bus. Changing routing must not clear all tails accidentally or leave unowned voices. Region geometry and antimeridian logic have no implementation yet.

Use one authoritative C++ audio engine for both live and offline operation. Render fixed sessions with no network, UI, or database calls inside the processing loop. Test complete rendered output/event schedules across block sizes, seek/loop/reset and sample rates. Claim bit-identical audio only for an explicitly qualified implementation/platform; event/mapping determinism is the immediate contract.

React/TypeScript plus Three.js inside a native webview is a coherent arrangement, and `ui/contracts.ts` provides a useful starting point. Keep the UI adapter capability-gated and host state authoritative. Add version negotiation, maximum payloads, cancellation, native push/batch refresh, generation rejection, full state resynchronization and parameter gesture feedback as real methods arrive. The current contract has no history-freeze method, session mutations, region editing or render API; it is intentionally a proposal, not a complete backend.

Bundle all assets locally, prohibit external navigation in a privileged native-integrated browser, route external links through a nonprivileged host action, and preserve the collector token boundary. Check the selected browser backend before use. Throttle or destroy/restore the hidden globe deliberately; preserve native state across browser teardown. Test multiple plug-in editors, context loss, software rendering and a usable 2D fallback. A UI frame target is a measurement budget, never evidence that audio is safe.

## Phased scope and go/no-go gates

| Phase | Concrete deliverable | Gate |
|---|---|---|
| 0. Preserve collection and repair semantics | A1 repair; validated metadata envelope; common schema fixtures; self-contained native storage tests | No owner workflow interruption; all existing tests plus counterexamples pass; no new source requests required. |
| 1. Native audible/render proof | Verified frozen native session → deterministic mapping/admission → one voice → offline WAV and simple standalone audio controls | Same actions across block sizes/speeds; no callback allocation/I/O/lock; bounded burst behavior; correct stop/seek/panic; finite output and declared headroom. |
| 2. H shell and local history | External UI design connected through capability adapter; real map/event inspector; local species/region/time selection; requested/actual/unknown coverage | Every sounded gesture inspectable; no fake coverage; no tokens in web content; bundled assets; editor hidden/open stress does not compromise audio. |
| 3. Native acquisition/writer parity | Source adapters, transactions, sessions, IPC, backup/restore and recovery in portable C++ | Isolated cross-language corpus covers deduplication, corrections, scopes, partial pages, errors, clock changes, crash, disk limits and schema migration. Controlled handover only after the owner's active recording ends. Never two writers. |
| 4. Compose and preserve | Small voice palette, density/diversity, capped regional sends, full musical-session snapshot, offline render/export | Reopen with network/collector absent; selected actions, routing and settings preserved; tails/durations/overload policy repeatable; uncertainty handled. |
| 5. Broader data and VST3 | At least one evidenced individual-track/history route, multi-day collector qualification, shared VST3 engine and packaging | Zero-charge permitted access; measured freshness and gaps; 48–72-hour evidence; Ableton state/automation/transport/offline-bounce tests; multi-instance collector behavior. |

The production requirement is portable C++; the Python prototype remains a collection tool and parity reference during migration. A verified frozen-session audible proof need not wait for a complete collector rewrite. Conversely, a prototype bridge must not become an undeclared permanent Python production dependency.

For the first useful release, prioritize local historical selection, live/replay honesty, a good bounded voice, one/two regional effects and portable recall. Defer universal worldwide ingestion, many speculative connectors, generative gap filling, an extensive effects suite, AU/MIDI and elaborate terrain/cloud rendering until those gates pass. Worldwide breadth and a confirmed individual-track source remain explicit product goals, not implied by the existing catalogue.

## Zero-cost and primary references checked

The inspected CMake targets require only C++ and SQLite; the Python runtime uses the standard library. No paid service/dependency is introduced by the current implementation. Source technical accessibility still does not establish redistributable data rights.

JUCE's official licence file confirms AGPLv3 and commercial alternatives. Select and record a distribution route compatible with the zero-cost requirement before committing a shipping wrapper; do not treat the presence of a free download as an approved closed-source distribution plan. Keep the core independent so the choice does not force a rewrite. [Official JUCE licence](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md)

JUCE documents local resource providers, native integration, hidden-page lifetime and backend support checks. Native integration should be confined to controlled content; requesting WebView2 alone can silently fall back when unsupported. These are reasons for an early shell proof and explicit backend validation. [Official WebBrowserComponent options](https://docs.juce.com/master/classjuce_1_1WebBrowserComponent_1_1Options.html)

Use a supported consistent database snapshot mechanism for migration/backup rather than copying a live main file. SQLite documents an incremental online backup API suitable for taking a snapshot while limiting writer interruption. [Official SQLite backup API](https://www.sqlite.org/backup.html)

No current provider rights, live availability, new JUCE build, new DAW test, listening test, or multi-day soak is established by this audit. Those remain explicit gates.
