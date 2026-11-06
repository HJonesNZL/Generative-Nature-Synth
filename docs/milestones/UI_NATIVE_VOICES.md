# Native Sound Studio voices

Implemented 7 November 2026. Sound Studio reads the native voice library, edits the selected definition, and saves/reverts through the C++ host. The illustrative demo retains its proposed synthesis families and parameters; they are not offered as implemented native controls.

## Behaviour and ownership

The native host supplies one implemented type, `struck` (a three-partial tone), and four parameter descriptions. React renders the host's ranges, curves, units and optional quantization step rather than maintaining a second native preset specification.

| Parameter | Unit/range | Curve | Default |
| --- | --- | --- | --- |
| Transpose (`tuning`) | integer −12…12 semitones | linear, step 1 | 0 |
| Attack | 0.002…0.100 seconds | logarithmic | 0.008 seconds |
| Decay (`decay`) | 0.100…3.000 seconds, T60 | logarithmic | 0.650 seconds |
| Voice gain (`gain`) | −60…0 dB | linear | 0 dB |

The envelope graph shows attack followed by exponential decay to −60 dB over T60. It does not invent sustain or editable release controls; the host retains its fixed 20 ms terminal fade. Parameter changes affect new notes; sounding notes and tails retain their settings. Voice edits do not ask for device restart, transport preparation or generation changes. The global master remains fixed at −18 dB.

The host owns at most 513 definitions including `default`. Species IDs are `species:` plus the provider species ID. Native species definitions are independent (`inheritsFromId: null`); the default is a fallback for animals without a species definition. The current native fallback presentation uses group scope and target ID `native`; the UI uses native capability context to describe fallback semantics. It never describes species voices as inheriting this definition. Individual overrides, alternative synthesis types, editable mappings, selected-event audition and region effects remain capability gated.

## Boundary and failure policy

`ui/contracts.ts` owns `VoiceDefinition`, parameter/type metadata, library entries, and the five native methods: `getVoiceLibrary`, `getVoice`, `setVoiceParameter`, `saveVoice(expectedRevision)` and `revertVoice`. Provisional/demo consumers re-export these shared types. `HostClient` validates identities, decimal revision strings, finite normalized values, parameter ranges/curves and bounded library shapes before exposing replies.

The library refreshes when prepared material or catalogue species identity changes, including species registered from an older saved piece while Studio remains open. Each parameter owns a serial begin/change/end queue. Intermediate changes coalesce while a request is pending. `flush()` closes an outstanding gesture and waits for all acknowledgements. Save, Revert and Undo/Redo drain every parameter queue before their next mutation. After a failed gesture, editing, Save and Undo/Redo require explicit recovery. Revert remains available even for an otherwise clean definition, drains outstanding work and clears the failure only after the native host acknowledges recovery. The UI explains that a worker restart can discard unsaved drafts. If Revert reports an error, the UI refreshes the authoritative definition because durable revert and audio publication can have different outcomes.

Only successful host acknowledgements change displayed dirty/revision metadata. A temporary knob preview is shown while sending. Accepted canonical values replace requested values, including integer transpose quantization. A failed change closes with the last acknowledged value and restores that value in the display. Requests belong to the mounted voice; late A replies cannot update B. Leaving a voice closes its old queues without cancelling an audible mutation or retiring the development worker. UI Undo/Redo records accepted begin/end values, stores at most 50 successful unsaved parameter edits, suppresses replay history and clears after successful Save/Revert. Dirty metadata and its per-voice revision fence live in the existing shared app state (at most 513 entries), so late acknowledgements and Atlas navigation preserve the correct badges. Independent parameter acknowledgements cannot rewind newer global dirty/revision metadata. Full definition reads use local activity and request-order fences; acknowledged Revert can therefore replace a prior worker’s higher revision after restart and reset notification echo counters. No authoritative preset data is duplicated in localStorage.

The native host and its SQLite adapter own drafts, durable compare-and-swap revisions and persistence. Only saved definitions return after process restart. `Start Atlas UI.command` exports `ATLAS_VOICE_LIBRARY`, defaulting to canonical `.state/voices.sqlite3`. The development bridge forwards this environment variable and does not add arguments unsupported by the read-only CLI. The wildlife library and voice sidecar remain separate.

## Verification

Recorded results: [UI evidence](../../evidence/native-voices/ui-checks.json).

- TypeScript typecheck passed; native and illustrative demo builds passed; bundle separation, CSP, licences and lazy-globe checks passed.
- UI unit suite: 21/21 passed. Focused gesture checks cover serial/coalesced flush, canonical revisions, closing failed changes with acknowledged values, thrown errors settling waiters, malformed/wrong-identity voice replies, valid 264-character prefixed voice IDs, and retiring queued edits after failure without unmatched closing events.
- `ui/app/tests/native-voices-browser.mjs`: browser-injected deferred native-shaped fixture passes metadata/control gating, AD/T60, Save/Revert/Undo drain ordering, canonical quantization, species-switch isolation, failed-edit rollback explicit Revert recovery without a dummy edit, rapid accepted Undo/Redo, failed history exclusion, late dirty badges, reverse parameter response settlement, host-local revision reset, stale full-read rejection, Atlas navigation, and preparation-triggered species refresh.
- `ui/app/tests/native-voices-integration-browser.mjs`: actual C++ `atlas-studio-fixture-host`, UiHost and SQLite sidecar pass library/control reads, acknowledgements, Save, owned process restart with saved-only recall, Revert and independent fallback. The script creates and removes its own synthetic wildlife library and separate voice sidecar under the system temporary directory.
- Existing readiness voice and voice-gesture probes remain regression checks for delayed load/save/revert/type isolation and closing a deferred old-voice gesture after selection changes.

These checks do not qualify subjective sound, physical-device output, Ableton or plug-in operation. No physical audio device was opened for this UI milestone. The existing explicitly guarded real-device smoke now forces a disposable sibling voice sidecar; it was not run for this change.

To run the native integration check, build `atlas-studio-fixture-host`, set `PLAYWRIGHT_MODULE` if needed, and run `node ui/app/tests/native-voices-integration-browser.mjs` from the repository root. It manages its own isolated Vite server and process restart. The deferred fixture uses an already-running isolated UI server (`ATLAS_TEST_URL`, default port 5187) with every host response injected in the browser. Never point tests at the owner's libraries.
