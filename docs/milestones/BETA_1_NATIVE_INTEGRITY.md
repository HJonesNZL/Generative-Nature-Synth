# Beta 1: native event-content integrity, 7 November 2026

This is the first implemented slice of M1, not completion of the full milestone. The branch is `beta-1` because Git does not allow spaces in branch names. The initial project baseline is commit `0cfab7f` on `main`.

## Contract

The read-only C++ library now verifies the stored event-content SHA-256 before constructing a replay timeline. It reads session metadata and event rows within one SQLite read transaction, validates checksum syntax, count/ordinals and extracted text types, and rejects altered payloads or revision sequences. Each library instance belongs to one worker; concurrent workers use separate connections. Hashing, JSON/database access, errors and allocations remain off the audio callback.

The existing schema-v2 Python writer persists canonical JSON: sorted keys, compact separators, ASCII escapes including Unicode surrogate pairs, finite JSON numbers using Python's representation. For each ordinal row the hashed bytes are exactly:

```text
{"event":<stored event_json>,"original_seq":<decimal signed-int64 sequence>}\n
```

Here the final `\n` means one newline byte; there is no newline within the wrapper except escaped event strings. Rows are concatenated in ordinal order. An empty session hashes the empty byte string. The native reader preserves the original stored JSON bytes, including floating-point spelling and escapes, rather than converting through a different serializer. It therefore matches actual schema-v2 writer and imported-bundle output. Reformatting/reordering stored JSON fails closed even if Python would re-canonicalise the same semantic value and accept it. This stricter storage-byte contract is deliberate and tested; arbitrary third-party JSON serializers are not qualified. A later versioned portable envelope needs its own canonical representation/migration.

The CLI reports `sha256-event-content-verified; metadata-envelope-unverified` and carries `capture_truncated` into replay output. Checksums detect changed content; they do not authenticate providers or an attacker who can replace both content and checksum. The private SHA-256 implementation follows the standard operations/constants in [NIST FIPS 180-4](https://csrc.nist.gov/pubs/fips/180-4/upd1/final); no certification is claimed and no new production dependency was added.

## Evidence

The pre-fix native storage suite reproduced accepting changed payloads/sequences, missing or invalid hashes, silently reformatted stored JSON, non-text optional IDs and absent replay truncation. After implementation, all three CTest suites pass in Debug, Release and ASan/UBSan configurations on this Mac:

- Existing 17 core cases.
- SHA-256 empty/abc/multiblock/million-a known answers, chunked updates and digest-state preservation.
- 12 disposable native storage tests, including actual Python writer → export → import → native replay; Unicode/escaped characters, numeric spellings, nested values, sequences above JavaScript's exact-integer range, empty sessions, alteration, missing databases and read-only main-file preservation.

Normal tests no longer require the owner's database. Python 3.9+ is a development-test dependency when `ATLAS_BUILD_TESTS=ON`; it is not a native runtime dependency. The owner library/recordings were not opened or changed during this slice. The previously passing 50 Python tests were run after relocation; Python product logic did not change in this slice.

## SQLite runtime qualification

A real-writer fixture exposed a platform limitation rather than a checksum failure: this Mac's Xcode SDK reports SQLite 3.51 headers while `/usr/lib` loads Apple SQLite 3.43.2. That runtime failed to open a closed WAL-mode database without sidecars produced by Homebrew Python/SQLite 3.53.3. An ordinary read-write open created sidecars and made it readable, but the native reader deliberately does not perform that write fallback against user data.

The already-installed Homebrew SQLite 3.53.3 headers/library pass the unchanged fixture, including closed WAL operation. All beta verification builds explicitly selected that matching pair:

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Debug \
  -DSQLite3_INCLUDE_DIR=/opt/homebrew/opt/sqlite/include \
  -DSQLite3_LIBRARY=/opt/homebrew/opt/sqlite/lib/libsqlite3.dylib
cmake --build build/native -j 4
ctest --test-dir build/native --output-on-failure
```

Release uses `build/native-release -DCMAKE_BUILD_TYPE=Release`; sanitizers use `build/native-sanitized -DCMAKE_BUILD_TYPE=Debug -DATLAS_SANITIZERS=ON`, with the same SQLite overrides. Those locations are this machine's existing dependency, not portable hardcoded build requirements. Other platforms must select their qualified SQLite headers/library. No test was skipped to hide the system-runtime failure. Production packaging must pin/bundle and qualify an appropriate SQLite runtime; these local binaries dynamically depend on the installed Homebrew library.

## Remaining M1 work

Metadata-envelope hashing; historical query/origin preservation; shared complete Python/C++ event semantics; requested-window versus trim policy including empty selections; broader malformed/storage fixtures and supported-format migration. Python replay truncation propagation also remains open. Native verification of stored event content alone does not close those requirements or qualify an audio instrument.
