# Soundscape library, backup and recovery — implemented

Completed as a local engineering milestone on 7 November 2026; [qualification and limits](../../evidence/project-recovery/qualification.md). The next planned milestone is [scalable saved-material browsing](NEXT_LIBRARY_BROWSING.md).

This follows the completed local standalone beta. Its goal is a reliable everyday project workflow without requiring users to manage app-owned folders manually. It does not expand data collection or introduce a paid service.

## Bounded scope

- Add native export of a self-contained `.atlas-scene` to an explicit new destination, preserving exact verified recipe bytes and provenance.
- Add a portable project backup/restore workflow containing selected verified scenes and independently saved voice definitions, with a versioned manifest, integrity checks, limits and conflict preview. Never replace an existing project or overwrite a wildlife database implicitly.
- Add a persistent completed-export list with recoverable status after restart and native reveal/save actions. A cancelled/failed render must never appear as a completed WAV.
- Improve fresh-start library selection and app storage diagnostics: distinguish no library, missing library, incompatible/corrupt state, absent device and available scene-only operation.
- Verify restart, render interruption, import during rendering, duplicate state ownership, and hidden/minimized application behavior. Keep physical listening and hardware sleep/wake checks as explicit evidence rather than assuming them from simulations.

## Ownership and implementation order

Keep scene/voice serialization and verification in portable C++; the shell owns choosers and file publication, the serial worker owns storage and native requests, and React owns acknowledged views. Freeze versioned backup and export-list contracts before parallel work. Use a storage/format agent, a UI/journey agent and a lifecycle/regression agent on GPT-6.1 SOL with disjoint files; the parent integrates and qualifies the exact packaged artifact.

1. Specify manifest, supported versions, bounded sizes, conflict outcomes and crash-safe publication. Inventory existing scene/voice formats before adding new persistence.
2. Implement scene export and verified backup assembly in staging; preserve sources and publish exclusively after validation.
3. Implement restore preview and explicit import under new local identities, with all-or-nothing metadata publication and recoverable disk failures.
4. Connect library/export-history UI and native dialogs; report capabilities and pending/failure states truthfully.
5. Test corrupted/missing/duplicate entries, interruption, full disk simulation, restarts, multiple instances and source-byte preservation; package and verify a copy outside the repository.

## Acceptance

A user can open the app, import/create a scene, save/edit its musical settings, render audio, find that export after restart, back up selected work, and restore it into a new empty app state without the original wildlife library. Restored audio/recipe identity matches the saved source. Existing state, collector data and owner files remain untouched by failed or conflicting imports. No new source feed, live-input latency target or public-distribution claim is bundled into this milestone.

## Execution contract — 7 November 2026

This milestone also includes the owner's fixed-circle NZ detail map. Dragging changes only the projected viewport, at the original enlarged scale. Coastlines, regions, uncertainty and selectable markers share the translation; geographic records and the globe orientation do not change. Clamp to recoverable country bounds, retain the viewport between screens, offer arrow-key navigation and Home/reset, and distinguish deliberate drags from marker clicks. Verify mouse, touch/cancel, clipping, reset, keyboard and both desktop layouts with synthetic records.

The project container is a single version-1 SQLite `.atlas-project`, containing selected sealed scene bytes and a SQLite-backup snapshot of saved voices. It excludes unsaved drafts, source wildlife databases, application/device settings and rendered WAVs. Limits are 100 scenes, 64 MiB per scene, 16 MiB voices and 256 MiB embedded total. A bounded manifest/checksum and semantic verification precedes preview and is repeated before restore. Restore uses a new nonexistent state folder, publishes exclusively, assigns usable local scene IDs and opens a separate application window. Existing projects never merge or overwrite. Details: `docs/engineering/PROJECT_ARCHIVE.md`.

Completed-export history is recovered from managed final WAV files, with no second persistence database. The worker scans at most 2,048 entries, validates exact WAV structure and embedded recipe while seeking over PCM, returns at most 100 items sorted by modification time, and reports exclusions/limits. Partial staging directories remain unfinished; corrupt/truncated files are not completed exports. This is structural validation, not a PCM authenticity claim. React shows history on demand inside Project. Native save/reveal paths accept opaque managed IDs only.

File operations use the shell's existing serial worker; dialogs stay on its message thread. Heavy backup/restore work explicitly disables listening. Library changes retain the independently owned render worker. New permanent feature rows are prohibited: File / Project and the existing Project popover carry these operations. All evidence uses disposable data; physical listening, hardware sleep/wake, public signing and other-platform runtime qualification remain separate.

Acceptance additionally requires the restored scene to be visible and recallable through UiHost, renderable without the source library, and to retain its source content hash and PCM. Restarted export history must recover a previously completed render. Invalid-library switches must leave the running render available; valid library/scene-only switches must preserve it. A checkpoint commit precedes final packaged verification and a final evidence commit.
