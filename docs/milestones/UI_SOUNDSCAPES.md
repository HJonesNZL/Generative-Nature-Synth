# Soundscape UI milestone

The UI captures the currently prepared piece and its complete active native sound recipe under a new name, recalls the copied scene, and exports that immutable saved scene to WAV. Atlas and Sound Studio share the same recalled material and transport. The native worker owns scene files, validation, synthesis and render jobs; React owns selection, transient feedback and acknowledged edit queues. No browser DSP, provider fetch or arbitrary file path is introduced.

## Behaviour and boundary

A capability-gated Soundscapes bar stays above both screens. Save is enabled only for prepared material and a nonempty name. It synchronously locks new gestures, drains registered parameter queues (including recently unmounted editors) and pending voice/type/master mutations, then submits `saveSoundscape`. A failed edit blocks capture. Individual voice presets are not implicitly saved: the scene captures all acknowledged active native drafts, including the current globals. New names create separate captures.

The saved-scene selector identifies what **Export saved WAV** will use. Later voice, transport or global edits do not become render inputs: `startSoundscapeRender` receives only the selected opaque scene ID. Save another soundscape to include later changes. The UI polls asynchronous native jobs, accepts matching notifications when offered, reports errors, supports capability-gated cancellation and retains the current job across screen changes. Old-job acknowledgements cannot replace a newer job; terminal state cannot regress to an earlier or contradictory state.

Open drains old queues before native recall replaces drafts. One atomic reducer transition selects `scene:<opaque-id>`, adopts the returned preparation, restores the exact rational speed and time basis, clears old event/voice selection, history results and dirty flags, and advances the voice context epoch. Header gain previews, output-enablement context and old transport errors clear together. The editor remounts and the map/grain/catalogue request the copied scene material. `usePlayback` adopts the returned preparation once, avoiding a second restore. Later speed/basis changes prepare that scene ID while retaining current host drafts. Recall never sends Play or enables audio.

The recalled source session carries original observed/arrival bounds, origin, truncation and integrity labels. Non-integer recalled speeds are shown explicitly instead of displaying the numerator as an integer multiple. The native host remains responsible for verified scene loading, quiet atomic replacement and material availability without consulting the original source session.

## Experimental species presets and controls

Host-described native voice types accept struck, whistle, reed, bell, air and pulse. The existing illustrative pluck/resonance design types remain accepted for the independent demo. The parameter boundary accepts host-defined ranges and Hz display units, supporting the original four controls and brightness, roughness, pulse rate and pitch sweep without duplicating native unit mappings.

`listVoicePresets` and `applyVoicePreset` are capability gated. The editor shows the returned species-bound candidates with explicit research, nonvocal ecological-metaphor or unresolved-taxon status. Applying waits for acknowledged parameter gestures, accepts a complete matching voice reply, clears obsolete undo/redo history and changes only the draft. It does not save, start playback or assert owner listening approval. Native code enforces the exact species/alias binding.

## WAV delivery

The development bridge exposes `GET /__atlas/exports/<32-lowercase-hex-artifact-id>`. It requires a loopback peer, the exact local Host, an exact same-origin Origin when supplied, and same-origin fetch metadata when supplied. Only a regular `<artifact-id>.wav` in the configured application output directory can be opened. Directory symlinks, file symlinks, traversal, queries, foreign origins and other methods are rejected. The route streams an attachment through an opened file descriptor with `O_NOFOLLOW`; no browser-supplied path is interpreted. Backend publication/no-overwrite and format validation remain native responsibilities.

`Start Atlas UI.command` configures `ATLAS_SOUNDSCAPE_DIRECTORY` to `.state/soundscapes` and `ATLAS_EXPORT_DIRECTORY` to `Exports`, with explicit environment overrides. It does not start or stop collectors. The bridge resolves one absolute export directory once and passes it explicitly to the native worker, including direct development launches without caller environment configuration. Native publication and HTTP delivery therefore use the same directory. Scene save/recall get a bounded 30-second bridge/UI response window; render work is asynchronous rather than held in a request.

Unmanaged scene filenames are represented only by a bounded aggregate count and a visible notice; their paths never become UI IDs. Opaque-ID files with damaged contents remain individual unavailable entries.

The DTO boundary validates scene identities, counts, exact speeds, hashes, copied-session bounds, preparation correlation, render state/progress and safe artifact identifiers. Public archive URLs from sound-design research are never samples or export inputs.

## Verification

- TypeScript strict typecheck passed.
- Vite production build passed, using a disposable output folder.
- All 25 TypeScript/unit/loopback bridge checks passed after adding capture races, failed-edit handling, malformed scene replies, WAV route traversal/symlink/origin checks and the direct-development no-environment child export-directory assertion.
- The full eight-scenario synthetic component browser suite passed with zero page errors and zero native fallback requests. It verifies deferred old-voice work, material error recovery, old cancel/new job races, capability gating, polling, cancellation failure/retry, stale progress and screen continuity.
- `native-soundscapes-browser.mjs` is a disposable real-native integration regression. It captures +12-semitone drafts, changes the current voice to −12, exports the saved scene, recalls it silently, checks restored settings, compares the WAV bytes before/after recall, and verifies same-scene speed preparation preserves later drafts. Run with the rebuilt scene-capable `atlas-studio-fixture-host` and external `PLAYWRIGHT_MODULE` when needed. The unknown-species fixture run passed all six checks: a valid 4,320,598-byte WAV was downloaded, exports before/after recall were byte-identical, and no page errors or rejected native replies occurred. The known-species rerun also passed: a clearly synthetic House sparrow fixture exposed the researched preset, a brightness edit was replaced by the exact initial factory recipe on Apply, no voice Save or Play was sent, and the valid saved scene remained usable alongside an unmanaged-filename notice. All six scene workflow checks passed again with byte-identical 4,320,598-byte WAVs, zero page errors and zero rejected replies.

The native-voice synthetic component suite also passed all 15 checks. The existing real-native voice browser regression passed with eight controls, canonical acknowledgements, Save/Revert, independent fallback and owned-process restart persistence; it reported no page errors or rejected native replies. The saved-scene UI was visually inspected at 1700×1100 with eight native controls and the saved material context intact.

These are engineering and synthetic playback checks. They do not establish natural-call resemblance, owner listening approval, audible hardware output, DAW operation or physical power-loss recovery.
