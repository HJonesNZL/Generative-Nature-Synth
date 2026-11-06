# Independent frontend change review

Reviewer: GPT-6.1 SOL agent. 7 November 2026 (NZDT). Read-only product/test review; only this evidence report was written. Scope is the current frontend diff for saved-material retry, voice request identity/gesture lifetime, persistent render-job state/polling/cancellation and GPS/receiver presentation. Supporting `HostContext`, `HostClient`, `useCoalescedGesture` and the new synthetic browser tests were inspected. This does not expand the audit to unrelated pre-existing controls.

**Final result: no remaining confirmed actionable regression found in this scoped diff after the two fixes below.** The browser tests qualify frontend behavior against an injected synthetic host, not the unimplemented native mutation/render APIs.

## Confirmed findings and resolution

1. **P2 — a late cancellation of job A could replace newer queued job B.** While cancellation A was pending, A could finish and enable a new render. Its delayed cancellation response then dispatched an unrestricted global `renderJob`, replacing B. Executing the exact pre-fix reducer with A-complete → B-queued → A-cancelled reproduced final job A/cancelled. The owner separated `renderStarted` from matching-ID `renderJob` updates and checked captured cancellation identity before applying a reply/error. The exact reducer replay then preserved B/queued. Independently rerun component browser regression also passed: A cancel is delayed, A completes, B starts, A cancellation resolves, and B remains queued through a second B status poll. The fixture uses distinct IDs and per-job states so cancelling A cannot falsely alter B's polled state.
2. **P2 — selection cancellation suppressed a required gesture end.** The new selection AbortSignal was passed to every parameter gesture. A keyboard edit queued begin/change/end; if A's begin acknowledgement was delayed and B selected, keyed unmount aborted the signal. The gesture queue retained end, but HostClient refused to send it with the permanently aborted signal. A read-only probe using the exact gesture hook/client with minimal mocked hook state reproduced host gestures `[begin]`, versus `[begin, change, end]` without that signal. The owner removed the selection signal from the voice-bound gesture sender while retaining loader/save/revert/type stale-response guards. Independently rerun component browser regression passed: delayed A begin is released after B selection, A receives ordered begin/change/end, B remains displayed, and none of those gestures targets B.

The existing same-job terminal guard was also inspected: queued/rendering updates cannot reopen a terminal job. The matching-ID update rule now protects the reducer even if a component's captured reply is stale.

## Checks actually run by this reviewer

Both commands ran from `ui/app` against the already running owner's local Vite server at `http://127.0.0.1:5173`. They launch a disposable browser page with a synthetic injected host and make no native bridge request:

```sh
PLAYWRIGHT_MODULE=/Users/henryjones/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs ATLAS_READINESS_CASE=voice-gesture /Users/henryjones/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node tests/integration-readiness-browser.mjs

PLAYWRIGHT_MODULE=/Users/henryjones/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs ATLAS_READINESS_CASE=render-cross-job /Users/henryjones/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node tests/integration-readiness-browser.mjs
```

Each exited **0**, with its named regression passing, `pageErrors: []` and `nativeBridgeRequests: []`. The cross-job case was rerun after per-job fixture correction and includes the next polling interval. The final redundant-condition cleanup in the reducer preserves the reviewed matching-ID/terminal logic. Source review of saved-material retry found that it clears the material store/outcome epoch, aborts superseded reads, reuses the bounded four-page loader and presents a stopped error with an explicit retry action; no additional new defect was confirmed there.

The UI owner separately reports **all eight** readiness browser groups passing on the latest fixture. That full-suite result is **owner-reported**, not independently rerun in this final review. This reviewer did not rerun final TypeScript/build/general browser qualification in this review; their existing evidence remains a separate checkpoint. Earlier numerical/audio qualification does not qualify these frontend mutations.

## Scope limits and integration gates

Current native capabilities still disable voice edits, audition, device transport and render-job APIs; CLI WAV export does not implement these UI commands. Native integration needs agreed parameter/gesture acknowledgement and recovery semantics, render-job/file identity and cancellation publication, transport generation/reconnect state and worker ownership before enabling those controls. An AbortSignal guard prevents stale UI state; it does not promise rollback of an already applied host mutation or immediate socket/processor cancellation. Same-voice concurrent mutation/undo reconciliation and unrelated region/history/master controls were not newly qualified by this scoped change review.

No owner database, collector, provider, credential, native device, listening, JUCE webview, DAW or production render command was accessed. The existing server belongs to the UI owner and was left running for that owner to stop. No product/test edits, commits or claim of complete frontend/backend readiness were made by this review.

## Reviewed file hashes

SHA-256 recorded after inspecting the fixes:

```text
ui/app/src/app/store.tsx: 1480be6c4494341eab12f7dc5403cd69a30a2388932a6e4b6e4ca30f2ae9e0a4
ui/app/src/app/useFeeds.ts: bbd3f79a4cfe1c1bb1bd88a8817c7f50de9d7a73dc0203a3d29687b0d3539515
ui/app/src/app/eventStore.ts: 5a9e48beb4c4104fb2663938e7c7a2735ac807445a57ff5750542502b9d112f5
ui/app/src/screens/studio/SoundStudioScreen.tsx: 0a421fed9e7b6158b321ee0953caab53bddd606e5a4bf90a197e1bed3e1529db
ui/app/src/screens/studio/VoiceEditor.tsx: c1a4fec68c362e1facd6dfdab40027fde04380744c9561f90d4db141dbf867e9
ui/app/src/screens/atlas/MapStage.tsx: 8b2ef034957cc9d816842c6a555fa7a4f58f616bf285e1a2fac2e4879238c006
ui/app/src/screens/atlas/RightColumn.tsx: 04f0aa36a454369a9d02984cf21eebc42dff610ae8f9c407ab2f462f88e01fb0
ui/app/src/screens/shared/GrainPanel.tsx: b4c42ba9e00fc6d339fa5146b77b2803ec3aa1f55ec44ecf5967a61a5200f4db
ui/app/tests/integration-readiness-browser.mjs: 60f2fc1bb8dc8a00144b858316bfed2f7735a8ee2a2e0f97e6a1870f897965cd
```
