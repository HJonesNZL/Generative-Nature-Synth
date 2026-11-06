# Independent native voice UI review

Reviewed by the independent SOL voice-store workstream on 7 November 2026, after the UI agent's source freeze. Scope: parameter queue ownership, asynchronous replies, selection/unmount, Save/Revert flushing, Undo/Redo, dirty/revision state, recovery after worker restart, and truthful native capability gating. The reviewer made no UI implementation edits.

**Result: no unresolved actionable findings in this scope.** The UI agent fixed the findings below; the reviewer checked the frozen implementation and independently reran the relevant regressions.

| Finding | Reproducer / final evidence |
| --- | --- |
| Undo captured optimistic end values and stale starts before acknowledgement | Holding the first change, pressing ArrowRight twice, then releasing originally displayed +2 st and Undo incorrectly produced +0 st. The independent browser rerun now produces +1 st. History derives from acknowledged begin/end values; failed gestures do not create entries. Persistent browser checks cover rapid Undo/Redo and rejected edits. |
| A late acknowledgement after selecting B left A's library badge clean | A delayed A edit now updates A's dirty metadata without updating B's mounted editor. Independent browser output: `Animal A\nspecies voice • unsaved`; B's value remains unchanged. |
| An older parameter response could rewind global dirty/revision metadata | Per-parameter revisions govern value merges; the newest voice-wide revision governs dirty metadata. An independent delayed older tuning-end response arriving after a newer gain edit leaves Save enabled and the unsaved indicator visible. Full-read activity/read fences reject delayed snapshots after local activity. |
| The client rejected valid prefixed IDs at the species boundary | A 256-byte ASCII species ID produces a 264-byte `species:` voice ID. The original client returned `Malformed host result`; the final client accepts it. The persistent client unit test includes this boundary. |
| Dirty badges disappeared when the entire Studio route unmounted | Dirty metadata now lives in the existing AppState, bounded to 513 entries, and survives Studio → Atlas → Studio. The frozen persistent browser suite verifies this route transition. No parameter/preset content is copied into browser persistence. |

The final recovery path was also reviewed and exercised independently: a synthetic worker counter reset to zero during a rejected change blocks Save and parameter controls; explicit Revert accepts the authoritative lower-revision saved definition, clears recovery and echo-revision state, and permits a subsequent acknowledged edit. Successful authoritative reads reset revision baselines; in-flight/local activity fences prevent stale full snapshots from overwriting edits. Save/Revert drain outstanding gestures; a failed gesture retains at most its matching closing end and discards later queued gestures. Native controls are driven by capabilities and host metadata: four implemented parameters and the single `struck` type; unsupported type/mapping/master/render actions remain unavailable.

## Independent verification on frozen sources

- Bundled Node `--experimental-transform-types --test tests/voice-gestures.test.mjs`: **6 passed, 0 failed**.
- Bundled Node + bundled Playwright, headless installed Chrome, `ui/app/tests/native-voices-browser.mjs` against the isolated loopback fixture on port 5187: **15 reported checks passed, zero page errors**. Includes queue flushing, canonical values, rapid Undo/Redo, failed history, selection isolation, dirty badges, reordered acknowledgements, lower-counter recovery, stale full reads, route retention and newly prepared species refresh.
- Additional disposable independent browser repros verified rapid Undo, delayed old-voice badges, reversed cross-parameter replies and counter-reset recovery. A direct HostClient boundary probe verified the 256/264-byte ID case.

These browser checks inject a native-shaped synthetic host. They do not use an owner database, a real preset sidecar, a physical audio device or a DAW, and do not establish listening quality. The parent workstream owns real C++ protocol/build qualification; that evidence is separate. An earlier full-browser attempt correctly exposed the fixture's stale Revert revision; the fixture was corrected to advance revisions like the native store before this final passing run.

## Frozen SHA-256 fingerprints

| File | SHA-256 |
| --- | --- |
| `ui/app/src/app/gestureQueue.ts` | `4ff360506b4334a3f63635510d53d268005949df7ee9cf5a0e11ea439b796d8f` |
| `ui/app/src/app/useCoalescedGesture.ts` | `426e69492c0683bc29777d87e5df460ca3db498cebc863687c9df6d562092f6d` |
| `ui/app/src/screens/studio/VoiceEditor.tsx` | `96221f43462d41d543124e76ed760998e671a8f583a8b9433eab595eae3c6e95` |
| `ui/app/src/host/client.ts` | `f88d7945937809f29b89f9f8fae55c410d5aa187f68a989953f7c19dbecc4e28` |
| `ui/app/src/screens/studio/SoundStudioScreen.tsx` | `1aeae93910272b0f0eaca4c7d1cea5be7eb2cee375fdd818be42ceb3281cd7df` |
| `ui/app/src/app/store.tsx` | `c7c952be362b3bb2ad2c81c6ec09d9a6171078cc0a35f35bd80e80efd0c25f1d` |
| `ui/app/tests/native-voices-browser.mjs` | `5e087c19bc9442f67e27cccdb4ef833b4ea8d8a16e386114e69ea610b22f7ee7` |
| `ui/app/tests/voice-gestures.test.mjs` | `a2ccd841a69dd7119446b927a51518b563b2d21ec73e0d4b325dbcdbd9b8d347` |
