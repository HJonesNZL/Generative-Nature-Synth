# Next milestone: soundscape library, backup and recovery

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
