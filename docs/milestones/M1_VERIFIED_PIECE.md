# M1 — verified native data piece

Completed on beta-1, 7 November 2026, in the qualified macOS arm64 fixture environment. This closes the M1 data/session gate in BUILD_PLAN.md. It does not claim an audio engine, JUCE wrapper, GUI, native collector or distributable application. Implementation checkpoints and their commit IDs are indexed in M1_BUILD_LOG.md; machine-readable evidence is evidence/m1-verified-piece.json.

## Delivered behaviour

- Schema 3 adds an exact-byte SHA-256 metadata envelope over all stored session interpretation/provenance fields and the event-content hash. Native replay verifies both within one SQLite read transaction and returns that same metadata to the CLI. Python replay/export also verifies a single read snapshot. Altered events, sequences, ordinals, counts, bounds, seed, origin and recording context fail closed.
- New freezes and imported copies are sealed. Schema 1/2 writer migration is transactional and has rollback tests. Existing frozen sessions are not retroactively sealed; metadata remains explicitly unverified. The native adapter supports schema 2/3 read-only. Future database/envelope versions and partial seals are rejected. No owner library was opened or migrated to obtain this evidence.
- Bundle v2 preserves original query, selection policy, seed, timestamps, source/version context, recording/interruption state and truncation. New import ID/time are local; one original provenance record survives repeated imports without nesting. Supported v1 imports retain legacy-unverified origin. Imported material never enters live source revisions.
- Complete stored-event v1 validation independently enforces types, required fields, bounds, UTF-8, identity namespaces, coordinate pairs, kind/location semantics, station/individual distinctions and confidence meaning. The common 69-case corpus has explicit expected outcomes. Native calendar arithmetic now safely covers UTC years 1–9999; exact epoch regressions fix a discovered signed 32-bit intermediate overflow.
- A bounded historical observation query defaults to preserve_requested: retain the full half-open requested interval, including leading/trailing gaps or no records. Explicit trim_to_material uses event extent; a single event occupies one microsecond and an empty trimmed selection has zero duration. One-sided/unbounded queries default to trim. Arrival replay retains separate receipt-time bounds. UTC query indexes preserve the original timestamp offset in the event; out-of-window imports fail before persistence. Policy/version and exact bounds are sealed and roundtrip unchanged.
- Replay/API responses expose truncation, metadata verification and observation-window policy. Empty material explicitly means no selected records, not demonstrated ecological silence. Extreme speeds/durations fail against the shared rational/int64 capacity instead of overflowing.

## Storage format

The metadata envelope is canonical JSON `{session: <all 12 persisted metadata fields>, version: 1}` with sorted keys, compact separators and Python-compatible ASCII escapes/finite number spelling. Options are at most 16 KiB and envelopes 32 KiB. Event payloads retain the canonical writer bytes; event row hashes remain the existing ordinal-order wrapper documented in BETA_1_NATIVE_INTEGRITY.md. C++ hashes stored bytes and compares the parsed envelope against actual columns, including the exact canonical options bytes. It does not attempt to recreate Python floating-point spelling.

| Input | Behaviour |
| --- | --- |
| Database schema 1 | Prototype writer upgrades atomically; native reader does not support it |
| Schema 2 | Writer upgrades to 3 without resealing old pieces; native read-only legacy replay |
| Schema 3, envelope 0 | Legacy metadata-unverified; event hash still required |
| Schema 3, envelope 1 | Verify event content and metadata binding; validate playback options |
| Bundle 1 | Validate bounded legacy metadata/events; preserve original legacy status |
| Bundle 2 | Verify envelope and content; preserve original context; seal new local copy |
| Future versions / partial seals | Reject; no guessed conversion |

Checksums detect changes, not provider authenticity or an attacker who rewrites bytes and hashes. Changing a legacy format's declared version is not a cryptographically authenticated downgrade boundary. Optional genuinely unknown ecological values remain unknown. Only existing local collected/imported material is selectable; this work does not establish new archive rights or universal historical coverage.

## Verification

- 70 Python tests on Python 3.9.6 / SQLite 3.43.2 and Python 3.12.13 / SQLite 3.53.3.
- Native Debug, Release and ASan/UBSan: all three CTest suites pass, including 18 core cases, SHA-256 known answers/chunking, and 18 storage/CLI integration tests. Those storage tests include all 69 common semantic cases, native/Python timing comparisons, exact 1900-based windows, empty/trim/truncation, interrupted imports, malformed resealed options, corruption and read-only checks.
- Atomic migration rollback, repeated roundtrips, legacy v1/v2 storage, original context preservation and no imported live revisions use disposable libraries. API tests use loopback only. No provider requests, owner recordings or design assets were used.
- Apple clang 17.0.0, warnings as errors; qualified Homebrew SQLite 3.53.3 headers/runtime. The system SQLite closed-WAL incompatibility from the initial slice remains a packaging qualification constraint. No fallback writes or skipped failing tests were introduced. clang-format was unavailable; local style was retained.

Tests are fixture qualification on this Mac, not Windows/Linux, packaged clean-machine, DAW, audible DSP, CPU deadline or long-soak evidence. Original audit reports and earlier evidence remain historical records rather than being rewritten.

## Next gate

M2: compile compact immutable musical actions off-thread, freeze overload admission independently of host block partitions, implement one bounded reference voice with release/steal/panic/reset ownership, and produce a headless WAV. Keep callback publication/retirement explicit. UI integration awaits the owner's Claude delivery and its M3 bridge/save/reopen gates. General native acquisition/library writes, backup/restore and pagination remain M4. No additional paid services, downloads or runtime dependencies were introduced by M1.
