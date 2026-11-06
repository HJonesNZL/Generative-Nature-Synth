# M1 build log

- `0cfab7f`: initial project/document/design baseline on main.
- `7a4778b`: beta-1 begins native event-content SHA-256 verification, consistent read snapshots, independent storage checks and Claude design handoff. Passed Debug/Release/ASan+UBSan with SQLite 3.53.3. Metadata, provenance, complete semantic parity and historical windows remained open.
- Completion contract: docs/milestones/M1_COMPLETION_CONTRACT.md. Implementation checkpoints follow below after verification and push.

## Metadata and provenance checkpoint

Schema 3 adds atomic metadata seals for new freezes/imports, preserving old sessions as explicitly unverified. Native reads verify envelope bytes and bind every metadata field within the same read snapshot used for events. Bundle v2 retains selection, original provenance, interruption and truncation across repeated imports; legacy v1 remains readable. Migration rollback is tested. Native CLI uses the metadata returned by the verified load.

Validation: 57 Python tests; Debug and ASan/UBSan CTest suites passed (17 core cases, SHA-256 vectors, 13 native storage cases). No provider requests or owner database changes. Remaining M1: complete event semantics, requested-window behaviour and final integration qualification. Checksums are change detection, not authentication. Qualified Homebrew SQLite remains required on this machine as recorded in the initial integrity evidence.

## Event semantics checkpoint (follows 04f754d)

Both implementations enforce the full stored-event v1 boundary. The shared corpus has 69 independently labelled acceptance/rejection cases. Adapter ingress and Python frozen replay/export use the same Python validator; native replay has an independent C++ implementation using the existing SQLite JSON adapter. Reuse the RAII statement wrapper instead of duplicating SQLite ownership. Frozen replay/export now reads and verifies one consistent snapshot.

The edge corpus uncovered an existing signed 32-bit intermediate in native day-to-second conversion. Promote before multiplication and check UTC year limits; exact tests cover 1900, 2040 and years 1/9999. Validation: 60 Python tests, Debug and ASan/UBSan CTest suites (18 core cases, SHA vectors, 15 native storage tests including all 69 semantic cases). clang-format was unavailable; touched C++ follows existing style. No network requests, owner database or design changes. Remaining M1: historical requested-window policy and final integration qualification.

## Historical windows and M1 qualification (follows 1059336)

Bounded observation queries now preserve requested half-open intervals, including gaps and empty selections; explicit trim remains available through CLI/API. The window/version/bounds are sealed. Arrival bounds remain separate. UTC indexing preserves original offset timestamps, invalid boundaries and out-of-window bundles fail before persistence, and replay carries completeness/metadata integrity. Native validation rejects malformed interpretation options even when hashes are recomputed. Prototype/native versions align at 0.3.0; rational/int64 timing capacities agree.

Final qualification: 70 Python tests on both 3.9.6/SQLite 3.43.2 and 3.12.13/SQLite 3.53.3. Native Debug, Release and ASan/UBSan pass all three suites: 18 core cases, SHA-256 known answers and 18 native storage tests including the 69-case shared corpus. Final storage additions were rerun in each configuration. No provider requests or owner library access; owner design ZIP untouched. Source hashes and environment/limitations are in evidence/m1-verified-piece.json. M1 is complete in this qualified macOS environment; M2 audio, UI, native writer, platform packaging and DAW gates remain.
