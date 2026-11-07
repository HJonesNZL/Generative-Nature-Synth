# Portable project archive, revision 1

`atlas/projects.hpp` is a worker-only C++20 storage boundary. It backs up selected sealed soundscapes and independently saved voice definitions, verifies the complete backup for preview, restores into an entirely new state directory, and exports a scene without changing its file bytes. It performs filesystem, SQLite, hashing and allocation work; it never runs on the audio callback. The shell owns user choosers and the serial storage worker owns calls and errors.

## Included material and public behaviour

- `createProjectBackup(voicesPath, scenePaths, outputPath)` snapshots the saved voice database with SQLite's backup API, including committed WAL material. It copies selected scene file bytes, validates all embedded material, then publishes one new `.atlas-project` file. In-memory unsaved voice drafts are excluded. An empty voice path creates an empty valid voice database; a nonempty missing path fails.
- `inspectProjectBackup(backupPath)` verifies every byte hash, the manifest and the semantics of each sealed scene and saved voice database. Its summary contains scene count, independently saved voice count, total embedded file bytes and ordered scene names. It does not modify the archive or application state.
- `restoreProjectBackup(backupPath, newStateDirectory)` repeats the full verification and publishes a new directory containing `voices.sqlite3`, `soundscapes/<32-lowercase-hex-id>.atlas-scene` files and an empty `exports/`. Local identities are the first 32 lowercase hex characters of SHA-256 over the verified manifest hash, a colon and the zero-based scene ordinal. They satisfy the UI managed-scene identity contract; collision is checked before publication. Archive entry names remain deterministic `scene-NNN.atlas-scene` paths. Captured recipe names, source identifiers, provenance and internal content hashes remain unchanged.
- `exportSoundscape(source, destination)` reads one sealed scene into private staging, fully validates it and exclusively publishes the exact copied file bytes at a new `.atlas-scene` path.

Settings, selected wildlife-library paths, source databases, unsaved drafts and rendered audio/export history are outside this archive. A scene's captured voice settings stay part of that immutable scene; independently saved voice presets are a separate snapshot. No merge or automatic active-state switch occurs in this storage API.

Errors are observable `ProjectError`s with invalid-argument, missing-source, existing-destination, unsupported-schema, corrupt-content, capacity and storage codes. No failure returns an empty successful project. Existing destinations, including empty directories and symlinks, fail without modification. Symbolic links anywhere in supplied paths and `..` components are rejected; callers can resolve benign operating-system aliases before presenting canonical paths.

## Single-file format

The archive is a SQLite database with application ID `0x4154504a` (`ATPJ`) and `user_version=1`, written in DELETE journal mode. The exact allowlisted table definitions are:

```sql
CREATE TABLE project_meta(key TEXT PRIMARY KEY NOT NULL,value TEXT NOT NULL) WITHOUT ROWID
CREATE TABLE project_entries(ordinal INTEGER PRIMARY KEY NOT NULL,path TEXT NOT NULL,name TEXT NOT NULL,bytes INTEGER NOT NULL,sha256 TEXT NOT NULL,content BLOB NOT NULL) WITHOUT ROWID
```

There is exactly one metadata row, `manifest_sha256`. Entries have contiguous zero-based ordinals. Entry zero is `voices.sqlite3` with an empty name. Remaining paths must exactly match `soundscapes/scene-NNN.atlas-scene`, with zero-based scene numbers; their names must match the fully loaded embedded scene. No arbitrary path is unpacked. Unexpected schema objects, extra metadata, duplicate/gapped ordinals and unexpected entries fail. SQLite integrity and typed-value checks precede semantic loading.

Each entry stores its positive byte length, lowercase SHA-256 of its BLOB and the complete original file bytes. The manifest is SHA-256 over length-prefixed UTF-8 fields (`decimal-byte-length:field`, without separators): first `atlas-project-v1`, then each entry's path, display name, canonical decimal byte length and lowercase file digest in ordinal order. The outer manifest binds names, paths, sizes, order and content checksums. These checks detect corruption; they provide no signature or authenticity claim against someone deliberately authoring a new valid archive.

Import first copies bounded raw archive bytes into private staging and opens that isolated copy. External SQLite WAL/SHM files alongside an incoming archive cannot supply additional material. Embedded scenes and voices are written only under allowlisted local names and fully loaded through `loadSoundscape` and read-only `VoiceStore` respectively. Existing scene/voice schema support remains authoritative.

## Bounds and publication

The maximum is 100 scenes, 64 MiB per scene, 16 MiB for saved voices, and 256 MiB of embedded bytes. Incoming archive size is limited to 260 MiB, allowing SQLite overhead. SQLite length/SQL/column limits and the destination voice snapshot page limit add defensive bounds. Inspection needs temporary disk space for an isolated archive copy and its verified files; peak memory is bounded by the archive copy/read and SQLite buffers.

Assembly, validation and restore take place in disposable sibling staging directories. File publication uses an exclusive same-filesystem hard link; a destination that appears before publication is never replaced. Directory publication uses the operating system's atomic no-replace rename: macOS `renamex_np(RENAME_EXCL)`, Linux `renameat2(RENAME_NOREPLACE)` or Windows `MoveFileExW` without replacement. Standard C++ directory rename can replace an empty destination and is therefore deliberately not used. Unsupported filesystem/platform operations report storage failure rather than degrade to overwrite or a partial directory copy. macOS is qualified here; other platform branches require their own CI execution.

Ordinary exceptions remove staging and leave the destination absent. A process killed before publication may leave an unreferenced `.atlas-project-*` staging directory beside the chosen destination; the existing project remains unchanged. This release does not claim power-loss durability for every restored filesystem write, secure erasure of staging, or protection against a hostile process concurrently replacing ancestor directories. SQLite backup consistency covers saved voices; selected scene files are captured individually, not as a filesystem-wide transactional snapshot.

## Verification

`native/tests/project_tests.cpp` uses disposable fixtures only. It covers a committed WAL voice edit with a later unsaved draft, exact restored/exported scene bytes, verified summaries, empty archives, missing sources, duplicate/over-capacity selection, existing-file and existing-empty-directory conflicts, source and ancestor symlinks, traversal, malformed schema/metadata/entry names, missing/extra entries, checksums, truncated archives, and semantic corruption of embedded databases even after recomputing outer checksums. Failed restores leave no published state, existing files retain their original bytes, and no verification files leak into restored state.

POSIX tests fork a disposable child and lower only that child's `RLIMIT_FSIZE` to 1024 bytes. With `SIGXFSZ` ignored, real filesystem growth fails during backup, restore and scene export; each API reports a storage failure, ordinary staging is removed, no destination is published, and the original scene, archive, saved-voice database and active voice WAL retain identical bytes. A separate child uses the default `SIGXFSZ` action and is verified to terminate from that signal during backup file growth. Its unpublished staging remains disposable, matching the documented abrupt-interruption policy; the source backup still fully verifies afterwards. Parent resource limits are unchanged and core dumps are disabled in these children.

These are executed file-size-limit failures and signal interruption, not a claim of actual full-disk, power-loss or hardware-failure qualification. Tests do not force an intervening publication race; the no-replace operations and existing-destination cases cover that rule. The POSIX injection cases are conditionally absent on Windows, whose write-failure and interruption qualification remains explicit follow-up work. Additional malformed metadata fixtures exercise negative, signed-64-bit-maximum and noninteger entry byte counts plus a maximum ordinal before any unbounded allocation.
