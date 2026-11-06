# Native voice drafts and preset sidecar, revision 2

The portable VoiceStore keeps independent fallback and species drafts, explicit durable saves and atomic staged recipe restoration on one serial UI worker. Revision 2 retains the four established numeric controls and adds four numeric controls plus six bounded candidate synthesis characters. The physical engine settings are stored exactly, so saving or reopening a soundscape voice does not roundtrip unrelated values through normalized/logarithmic UI coordinates. The neutral fallback remains separate from the global master level, whose native-host initial value is −18 dB.

The implemented candidate palette is `struck`, `whistle`, `reed`, `bell`, `air` and `pulse`. These are shared engineering primitives, not a final product ceiling, a reconstruction of animal recordings or owner listening approval. New unpersisted species can start with an explicitly experimental researched candidate from the immutable catalogue; existing saved voices always win.

## Ownership and public API

`atlas::VoiceStore` owns mutable drafts, gesture state and one dedicated app-owned SQLite connection. Every method can allocate or perform storage work and belongs on the UI/control worker, never an audio callback. The engine owns its separate bounded numeric bank and acknowledged update/publication path. The constructor accepts a sidecar path, optional species catalogue and read-only/read-write access. `registerSpecies` atomically adds/renames catalogue entries. Saved species absent from the current source catalogue remain exposed under their unchanged species ID until a later catalogue supplies a display name. Names are presentation metadata and are not persisted as preset content.

Fallback plus species drafts are bounded to 513 total. Durable species rows are bounded to 512, plus the optional saved fallback row. Registration or restore that would exceed the live union reports `full` before changing any existing draft or name. Save independently enforces the durable capacity inside its transaction, including competing hosts. Unknown/duplicate IDs, unsupported formats and malformed values fail observably.

`getVoice` and `voices` return copied snapshots. A snapshot contains the definition ID/name, optional species ID, eight normalized UI values, complete physical VoiceSettings, exact integer saved/change revisions, dirty state and active-gesture state. `default` is the fallback definition; species definitions use `species:` followed by the unchanged provider species ID. There is no inheritance or implicit fallback-to-species parameter link. UI adapters report no inherited override IDs.

## Parameters and exact physical settings

| UI parameter ID | Normalized encoding | Physical engine value | Neutral default |
| --- | --- | --- | --- |
| `tuning` | linear, nearest integer semitone | −12 through +12 semitones | 0 |
| `attack` | logarithmic | 0.002 through 0.100 seconds | 0.008 |
| `decay` | logarithmic | 0.100 through 3.000 seconds, T60 | 0.650 |
| `gain` | linear | −60 through 0 dB | 0 |
| `brightness` | linear | 0 through 1 | 0.5 |
| `roughness` | linear | 0 through 1 | 0 |
| `pulseHz` | linear | 0 through 16 Hz | 0 |
| `sweepSemitones` | linear | −12 through +12 semitones | 0 |

`VoiceCharacter` is a separate discrete setting ordered `struck`, `whistle`, `reed`, `bell`, `air`, `pulse`, encoded as integer 0 through 5. Whole-profile staging/preset application can replace it together with all eight physical values. The numeric parameter gesture API does not pretend that character is a continuous normalized parameter.

Normalized inputs must be finite doubles in [0,1]. Tuning is quantized to the declared semitone step. Defaults and endpoints are handled exactly before evaluating logarithmic curves. A numeric edit converts only the edited parameter back to physical units and preserves every other physical value and the character exactly. UI reflection may compute normalized coordinates, but those coordinates are not used to reconstruct unrelated settings during save, stage or reopen. Dirty compares the complete physical tuple and character against the draft's saved/base settings.

The existing `validVoiceSettings` engine boundary validates all physical values and characters; storage independently checks SQLite column types, integer tuning quantization, revisions and row schema. Nonfinite, out-of-range, wrong-type or unknown settings cannot become silent defaults. AudioPlan separately checks the effective root/transpose combination against its supported range before audio preparation.

## Candidate base settings and saved precedence

When a species has no durable row, the store searches the immutable `speciesPresets()` catalogue by the exact unchanged species ID. A match supplies that species' complete experimental candidate as its clean base: `savedRevision=0`, `dirty=false`. This automatic initialization performs no durable write. Different matching species start from independent candidates. Unknown species, the explicit fallback definition and the unresolved-order neutral catalogue entry retain neutral settings; the store does not invent an animal identity or acoustic signature.

Editing makes a candidate draft dirty relative to that base. Revert on an unsaved species restores its candidate base. Explicit Save persists the exact current physical settings and character. On restart, a valid durable row takes precedence over the catalogue, including a neutral voice the owner deliberately saved. Migrated v1 rows preserve their prior audible settings and neutral added settings; a newly available catalogue candidate never replaces them. Display names and research/listening labels are supplied by the UI/catalogue, not treated as saved numeric content.

## Gestures, preview and audio acknowledgement

A parameter gesture follows `begin`, zero or more `change`, then `end`. Begin records gesture state without changing the parameter. Change/end apply the value; end closes the gesture. Different parameters may have independent active gestures. Each accepted phase advances the host-local integer change revision. Invalid enum/order, unknown voice/parameter and malformed numeric input fail without partial mutation.

`previewParameter` validates and computes the exact prospective snapshot without mutating the store. The host previews, submits the physical update to the engine, waits for acknowledgement and then applies the same edit on this worker. No intervening store mutation is permitted between preview and apply. A failed end gesture is closed at the last acknowledged value after the host disables/reconciles audio, preserving Save/Revert usability. Save and Revert reject open gestures. Read-only stores still allow draft edits; explicit Save reports read-only and preserves the draft.

## Atomic staged profile updates

`stageProfiles(profiles, optionalNames)` allocates and validates a complete prospective draft-map copy before audio preparation. It accepts a subset for a single-species preset or a complete captured scene bank. If a fallback profile is included, it must be first. Profile IDs are bounded and unique; names are bounded, valid and unique. Staging refuses any open gesture. New species read their durable row or candidate/neutral base before applying the incoming exact physical profile, so dirty/base revisions remain meaningful. Unmentioned drafts are retained unchanged. Staging itself does not change drafts, catalogue, revisions or durable rows; destroying an unused token abandons the operation.

`commitProfiles(DraftUpdate&&)` publishes by a `noexcept` map swap on the same worker after audio preparation succeeds. All restored profiles share one new change revision. The token belongs to exactly one store lifetime through a retained opaque identity, and records the base change revision. It cannot commit into another store, a replacement store or a changed local state. A successful edit, save, revert, species registration or catalogue rename makes an earlier token stale. An identical no-op registration leaves a valid token intact. A token cannot commit twice. Failed/stale commits return false without changing drafts or writing durable presets. The host must handle an unexpected post-audio-prepare stale result by stopping/reconciling audio; the serial worker normally prevents such an interleaving.

Retired draft-map strings and allocations are destroyed on the control worker when the token retires, never in a callback. This is a draft transaction, not an automatic preset save. The full self-contained soundscape boundary separately requires an explicit fallback and every event species profile.

## Durable v2 layout and transactional save

Application ID remains `0x41545653` (`ATVS`). New stores use database `user_version=2` and per-row `schema_version=2`. `voice_presets` is created as one `WITHOUT ROWID` table with columns in this order:

`voice_id, schema_version, saved_revision, tuning, attack, decay, gain, brightness, roughness, pulse_hz, sweep_semitones, character`.

The ID is text; schema/revision/character are integer columns; the eight physical values use real columns. Every read validates actual SQLite storage classes independently of affinity. Tuning is a finite integral physical semitone value, character is a supported integer enum and saved revision is a positive signed-64-bit integer. Unknown/generated/additional columns, invalid keys/rows and unexpected user schema objects are refused. Nonempty unrecognized databases and animal-library application/schema identities are refused without changing their data or schema. Values are bound parameters, never interpolated SQL or filenames.

Only explicit `save(id, expectedSavedRevision)` persists a draft. New definitions have saved revision 0; successful saves start at 1 and increment exactly, including values above 2^53. Revisions are bounded by SQLite's signed 64-bit range and the JSON adapter exposes them as decimal strings. Change revisions are separate worker-local integers; registration/renaming advances them only when state changes.

Save begins `BEGIN IMMEDIATE`, reads and validates the durable row, checks expected revision against both the local base and the durable value, checks capacity, writes one complete v2 physical row and commits. Only after commit does it update the clean base and acknowledge success. Another writer produces a visible conflict; busy/read-only/full/SQL/commit failure rolls back and preserves the draft. Revert reloads the latest durable row or the unsaved candidate/neutral base. A previously durable row that disappears is corruption, not a default restore. SQLite uses its rollback journal and `synchronous=FULL`; rollback checks are not physical power-loss qualification.

## V1 migration and concurrent initialization

Recognized v1 files contain the original seven-column layout and four normalized numeric controls. A read-only v1 open validates and decodes those rows without migrating or writing the file. Added controls use their neutral defaults and character is `struck`, preserving prior audible semantics.

A read-write v1 open obtains `BEGIN IMMEDIATE`, rechecks application/schema identity after acquiring the lock, and validates every legacy row and the complete capacity before any DDL. It retains exact saved revisions, decodes the four legacy normalized values using their established parameter semantics, creates the v2 physical table inside the same transaction, copies complete valid rows, drops the temporary legacy table and advances `user_version=2`. Failure rolls back schema and data together; a valid row cannot be partially upgraded because a later row is malformed. Concurrent hosts recheck identity/version after lock acquisition, so a host that discovers another host's completed migration validates v2 instead of migrating twice. Empty-store initialization uses the same recheck-after-lock rule.

There is no migration of the owner's wildlife database. The caller must choose an application-owned sidecar path separate from that library and create its parent directory. A read-only legacy connection encountering a later externally migrated incompatible row fails explicitly rather than treating v2 physical values as v1 normalized coordinates.

Errors carry `VoiceStoreErrorCode`: invalid argument, not found, gesture active, conflict, read-only, full, corrupt, unsupported schema, busy or storage. Adapters retain an observable failure and do not conceal it with fabricated empty/default success.

## Verification and limits of evidence

`native/tests/voice_store_tests.cpp` uses only automatically deleted temporary files. The existing adversarial cases remain: independent drafts/defaults, strict gesture preview, quantization, explicit save/reopen without autosave, catalogue recovery, durable CAS, read-only and busy rollback/retry, uncommitted rollback, live/durable capacity, malformed types/schema/generated columns, exact >2^53 revisions/exhaustion, concurrent-host capacity, owner-like database refusal, disappeared durable rows and literal opaque IDs.

Revision-2 cases add read-only v1 preservation; atomic v1-to-v2 migration of audible values and >2^53 revisions; malformed legacy rollback/retry with no residual table; exact eight physical values plus character across save/restart/revert; no drift in unrelated fields when one normalized parameter changes; abandoned/foreign/expired-store tokens; atomic profile revision publication; single-species subsets; stale-stage detection on edits/save/revert/register/rename/gesture; active-gesture refusal; no-op registration; invalid-stage atomicity and no accidental durable species writes. Candidate cases verify distinct clean Ruru/Kōtare bases, neutral fallback/unresolved order, unsaved Revert, absent autosave and saved-row precedence after reopen.

The full suite compiled with C++20 and project warning flags as errors and passed standalone ASan+UBSan after candidate integration. Parent CMake/CTest provides integrated verification. Synthetic protocol/output tests additionally exercise host/audio acknowledgement and recovery. None of these checks uses the owner's database, active collectors, a physical audio device or DAW session, and none establishes owner listening approval of the research candidates.
