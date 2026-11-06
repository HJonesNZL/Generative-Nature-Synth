# M1 completion contract — 7 November 2026

Branch: beta-1. The baseline main branch remains unchanged. This work does not access the owner library or active collector. The untracked design/Claude_Design_Handoff_2026-11-07.zip is owner material and is outside these code commits.

## Scope and checkpoints

1. Versioned metadata integrity, legacy migration and provenance-preserving import/export.
2. Shared event-semantic corpus with equivalent Python/C++ acceptance rules at stored-event boundaries.
3. Historical requested-window versus trim behaviour, empty selections, completeness reporting and final M1 integration qualification.

Each completed part is committed and pushed with rationale, actual validation and remaining limits. The milestone log records resulting commit IDs. Do not describe partial passing work as M1 complete.

## Storage and preservation contract

Database schema 3 adds session envelope version/JSON/hash columns. Existing schema 1/2 libraries upgrade transactionally when explicitly opened by the prototype writer; the native reader remains read-only and accepts schema 2 and 3. Migration does not reseal or reinterpret old frozen sessions. Legacy sessions remain explicitly metadata-unverified; new freezes and imported copies carry a version-1 envelope. Unknown future versions fail closed.

The envelope hashes a bounded canonical JSON object containing version and all stored session interpretation/provenance fields, including options, time bounds, seed, counts and the event-content hash. Native verification hashes the exact stored envelope bytes and binds its contents to the actual session columns in the same read snapshot. Python verifies the same envelope. Checksums protect against changed material, not an adversary replacing both bytes and hashes.

Bundle v2 preserves the original validated selection/options, timestamps, origin, recording/interruption state and version context. Local import identity/time are separate. Repeated roundtrips retain one original provenance record rather than recursively nesting imports. Legacy v1 bundles remain readable and retain an explicit legacy-unverified origin. Unknown fields, versions, wrong types, excessive size and duplicate JSON keys are rejected at import boundaries. No imported piece creates live provider revisions.

## Shared semantic contract

The complete stored event-v1 boundary validates field types/required fields, bounded strings/arrays/maps, explicit timestamps, finite numeric values, coordinate pairs/ranges, identity namespaces, kind/location pairing, confidence meaning and station-versus-individual semantics. A single positive/negative fixture corpus supplies independently stated expected acceptance to both implementations. Existing legitimate unknown fields remain nullable; no new ecological identities or precision are invented. Adapter ingress and frozen revalidation share Python validation; native replay independently enforces it.

## Historical window contract

For a bounded observation query, default playback preserves the requested half-open interval, including empty material and leading/trailing gaps. Explicit trim-to-material retains only the observed extent (empty material is zero length). Unbounded/one-sided queries retain material extent unless a complete requested window is supplied. The policy/version and exact bounds are saved and hashed. Arrival replay retains separate arrival bounds. Empty playback silence is never labelled evidence of ecological silence.

## Verification

Disposable legacy/current databases and bundles; corruption of each metadata category; wrong/missing/future seals; two export/import rounds; interrupted and truncated recordings; malformed event/schema corpus; exact requested versus trimmed timing; native/Python replay agreement; read-only native opens; no imported live events. Run affected Python checks and native Debug, Release, ASan/UBSan suites with warnings as errors and qualified SQLite. Preserve old evidence and write new evidence separately. No provider network tests or owner database migration are needed.
