# Frontend integration readiness — 7 November 2026

This is an implementation audit of the selected H2 Atlas/Sound Studio, not a redesign or a claim that every proposed control has a native backend. Scope: original `ui/app/HANDOFF_README.md`, `docs/CONTROL_INVENTORY.md`, `docs/INTEGRATION_GAPS.md`; canonical `ui/contracts.ts`; `ui/app/src/host/{contracts,provisional,client,nativeHost}.ts`; shared App/Header/store/HostContext/catalog/feed/gesture code; Atlas and Studio components. Historical handoff docs remain reference material and contain statements superseded by current implementation.

The present C++ UI host implements only `getCapabilities`, `getChanges`, `getCatalog`, `listSessions`, `prepareReplay` and `getMaterialEvents`. It is read-only and reports `audioAvailable:false`. The separate native offline WAV CLI now exists; no in-app render, playback, device, collection mutation, synthesis editing or effects API is implied. Live acoustic listening/detection records remain distinct from actual individual GPS/satellite tracking. Receiver coordinates never become animal fixes merely because an event has a station ID.

## Contract status

- **V1 shared presentation boundary:** `ui/contracts.ts` is the single exported DTO source. `ui/app/src/host/contracts.ts` re-exports it. Protocol version, request correlation, error envelope, UTC strings and decimal string int64 values are shared. Presence in this proposed V1 interface does not prove native implementation or settle future engine semantics.
- **Implemented read slice:** the six methods above have native validation/capacity/integrity behaviour. `getCatalog` and `getMaterialEvents` remain declared in `provisional.ts` despite actual native support; their supported subset is documented below. Promote through one agreed contract revision rather than duplicate definitions.
- **Provisional only:** all additional voice definitions/ranges, inheritance/reset, mappings, outcome explanations, region activity/mix/routing, collection/audio state, render jobs, write APIs and push notifications. Mock support is demonstration only. Proposed Studio attack/decay/gain/ADSR/types do not match the fixed M2 struck-tone engine settings and must not be relabelled as production parameters.
- **Boundary ownership:** C++ owns event/provenance validation, deployment/individual meaning, temporal compilation, density/admission, DSP, region membership and persistence. React owns selection, presentation, user gestures and capability/error states. No JS sound engine, WebAudio fallback, provider client, SQL or duplicate normalization is appropriate.

## Complete control → capability matrix

`V1` means shared interface; `P` means provisional. “No” means disabled/unavailable with the current native host, not permission to simulate it. Local-only controls require no native mutation.

| Area / controls | Contract / query or mutation | Current native support and readiness |
|---|---|---|
| Atlas / Sound Studio navigation | Local shared `screen` | Yes; same provider/stores survive; inactive screen unmounts |
| Live / Replay / History material | Local mode; `getChanges`, `listSessions`; V1 `queryHistory` | Live/Replay data yes; History disabled |
| Return to start; Play/Pause; Stop; All off | V1 `setTransport` | No; disabled; prepareReplay is verification, not playback |
| Clock / transport position | Local wall clock; P `transportChanged` | Wall clock yes; no native playhead/state query |
| Collect popover; 1/2/4 hours; play while collecting; Start; confirmed Stop | P `getCollectionState`, `setCollection` | No; explanatory popover; does not control existing collector |
| Listening switch | P `getAudioState`, `setAudioEnabled` | No; disabled |
| Master slider and output meter | P `setMasterGain`, `getAudioState` | No; unavailable meter, no fabricated native signal |
| DEMO states / automation simulation | Explicit mock-only menu | Absent in native bundle; never a capability substitute |
| Species search; species highlight; all-species reset | P `getCatalog` plus loaded EventViews; local filter/selection | Yes, sampled names plus loaded IDs; not all species/library |
| Source status/fetch/observation age | V1 `getSourceHealth` | No; unavailable, not inferred from polling |
| Source setup button | No settled API | Always disabled |
| Saved piece selection; retry session list | V1 `listSessions`, `prepareReplay` | Yes; newest 100 disclosed; sealed material verified |
| Replay speed 1/10/60/240; observed/arrival basis | V1 `prepareReplay` | Yes for timeline preparation only; rational string speed |
| History species/region/date/from/to/timezone/check | V1 `queryHistory`, `listRegions` | No; History entry disabled; NZ/UTC presentation proposal retained |
| History name; preserve/trim; freeze | P `freezeHistorySelection` → V1 `prepareReplay` | No; not inferred from saved-session reads |
| Globe/2D; zoom/reset; rotate; reduce motion | Local renderer/camera state | Yes; worldwide 2D fallback, no source acquisition |
| All loaded/current-window scope; shown/location count | Local bounded buffer/view | Yes; first live 200 revisions, retained/material 2,000, max 400 symbols/lower host cap |
| Receiver/fix picking; keyboard displayed-location list | EventView data; local event selection | Yes; selected/highlighted symbol priority; no tracks/interpolation |
| Region mix region select / activity / shares | V1 `listRegions`; P `getRegionActivity`, `listEffects` | No; unavailable, region membership stays backend-owned |
| Species solo/mute in region mix | P `setSpeciesMix` plus outcomes refresh | No; disabled |
| Assign shortcut / Open voice in Studio | Local navigation; P `getVoiceLibrary`, `getMusicalOutcomes` | Navigation possible; actual assignment/library unavailable |
| Selected/Incoming tabs; incoming record select | Local store; P `getMusicalOutcomes` optional | Records yes; latest 40 list; no native musical outcomes |
| Inspector kind/time/identity/location/confidence/source | EventView + catalogue | Yes; preserves original provenance; station-vs-fix wording now follows location kind |
| Grain lane highlight / nearest event pick / coverage | Local bounded display; history CoverageIntervals | Data display yes; six lanes, keyboard alternative through list; not scheduler |
| Studio group/species/individual library selection | P `getVoiceLibrary`, `getVoice` | No; individual entries require source identity, never microphone inference |
| + Assign animal voice | No settled create/assign API | Always disabled |
| Undo/Redo; parameter gesture/resets | V1 `setVoiceParameter`; P definition/specs | No; proposed unsaved per-view history, not host durable undo |
| Revert; Save voice | P `revertVoice`, `saveVoice(expectedRevision)` | No; revision conflict must remain explicit |
| Pluck/Air/Resonance/Bell type selector | P `setVoiceType` | No; not M2's implemented palette |
| Tuning/Attack/Decay/Sustain/Release/Tone/Gain/Pan knobs | V1 `setVoiceParameter`; P `VoiceParameterSpec` | No; ranges/proposals must be agreed with future engine |
| Envelope diagram | Local presentation of host-defined proposed values | No sound produced; not independent DSP model |
| Mapping toggles/table | P `getMappings`, `setMappingEnabled` | No; mapping/admission semantics stay native |
| Audition selected event | P `auditionEvent` | No; disabled; not browser sample synthesis |
| Render WAV; progress; Cancel | P `startRender`, `getRenderJob`, `cancelRender`, notification | No; separate CLI export does not implement these methods |
| Region drawing | No settled editing API | Always disabled |
| Region selection / effect on-off / send | V1 `listRegions`; P `listEffects`, `setRegionEffect` | No; no JS region classification or effect processor |
| Overlap / uncertain-position routing policy | P `setRegionPolicy` | No; default UI proposals are not acknowledged production policy |

Native read constraints: `getChanges` pages ≤500, UI requests 200 and retains ≤2,000; catalogue samples ≤2,000 and returns ≤500 species/stations, ≤128 sources; saved material pages ≤500 and UI holds first 2,000; listSessions UI max100. Selection/catalogue display must not be described as whole-library or source-coverage truth.

## Pre-edit findings and resulting fixes

Findings 1–4 were reproduced through actual components with a controllable synthetic host, then repaired in this slice. The same regression script rejects checkpoint `0779145` for all four categories. Findings 5–6 and the state gaps below remain future integration work, not native DSP failures. No capability was enabled by these repairs.

1. **Fixed — P2, native data preview: Inspector misclassifies GPS fix carrying station metadata.** Before this repair, `atlas/RightColumn.tsx` chose station-position wording solely from `event.stationId`, and Incoming rows preferred station labels. A valid `gps_fix`/`individual_fix` with station ID is still a measured animal fix; map/catalogue repair does not repair Inspector wording. The repair uses location kind for display semantics and keeps optional station metadata separate. The regression selects both records and checks measured-fix versus receiver text. Incoming labels and Inspector heading now use the same location-kind distinction.
2. **Fixed — P2, native data preview: saved-material failure falsely claims automatic retry.** Before this repair, `useMaterialFeed` set `feed.status='retrying'` on a failed page and returned without a timer or retry action. `MapStage` told the user that the feed was retrying/resumes. Re-selecting the same saved piece leaves the material key unchanged, so it need not re-run. The feed now reports an explicit stopped/error state and Atlas exposes **Retry material**, which clears/reloads the same material with existing cancellation and bounded paging. First-page and later-page injected failures recover without duplicate records; loaded material survives screen switching.
3. **Fixed selection race — P2 before enabling a voice backend: stale voice replies can edit the wrong selected definition.** The former `VoiceEditor` load path had no AbortSignal/selection guard; an A reply could replace B's displayed voice after selection B. `sendParam` bound the selected ID even when a stale definition was displayed; save/revert/type replies likewise applied after selection changes. The editor is now keyed to the selected voice; load/parent/save/revert/type/mapping requests use abort guards. Deferred A replies cannot replace selected B. Accepted gestures deliberately retain A identity and complete ordered begin/change/end after selection changes; they do not inherit the read/mutation cancellation signal. Mapping failure no longer falsely marks the voice dirty. This path remains unsupported natively; the tests qualify frontend branch behavior only.
4. **Fixed lifecycle defects — P2 before enabling render API: job lifecycle incomplete.** Before this repair, `StudioActions` disabled render only during `rendering`, not `queued`/start request; repeated clicks could start multiple jobs. Cancel lacked capability gating and discarded error replies. `getRenderJob` was declared but unused, so a host without push left jobs stuck. Job state was lost on Atlas/Studio switching. Define ownership/reconnect/query semantics with backend before integration; the implemented repairs now prevent pending/queued duplicate starts, gate Cancel by capability, surface cancel/status errors, poll the existing `getRenderJob` proposal when offered and retain observed job/pending state across screens. New-job acceptance is separate from same-job updates: late A replies cannot replace B. A terminal job cannot regress to queued/rendering from stale poll/push. Tests cover no-push polling, a late cancel-A reply after B starts and accepted A gesture closure after voice selection. No sequence/epoch exists in the proposed job DTO, so conflicting terminal results, host restart/job-ID reuse, duplicate progress ordering and reconnect recovery still need a backend contract. Polling resumes when Studio remounts; no claim is made that progress is continuously observed while Studio is closed.
5. **P2 before enabling transport: Return-to-start ignores acknowledgement/errors; notifications accept any generation.** Header drops seek result, while play/pause errors are surfaced. No current-state query exists for reconnect; stale transport notifications can overwrite a newer preparation. Backend contract must define generation/preparation identity and recovery first; UI must guard replies and show failures.
6. **P2 before enabling mutations: several error paths retain optimistic/stale state.** Parameter edits mark local dirty before acknowledgement without recovery; audio-enable failure is discarded; region-send failure is not displayed; master gesture error is not rendered and keyboard local value can persist. Voice library error/empty states are visually blank. Do not promise ready-for-engine mutation correctness based on the happy-path demo.

## State and cancellation checklist

| Surface | Existing handling | Gap / next qualification |
|---|---|---|
| Bridge connection | Connecting/backoff/failed+Retry; capability validation; request IDs; abort/timeout distinction | DTO payload schemas/notification bounds remain partial; capability reset during reconnect and old mutation replies need qualification |
| Generic reads | Loading/ready/error/unsupported; previous-value retention; key change aborts prior request | Consumers sometimes hide query errors/empty state; stale retained values need explicit display wording during disconnect |
| Live feed | Poll/backoff; abort cleanup; revision/store-ID reset; baseline/correction never re-pulse | Actual live source freshness not measured; same-store rewind/reset semantics require backend epoch contract |
| Saved material | Four bounded pages; omission count; abort on material/context change | Failed page now stops with explicit error/Retry material; total/page consistency still needs schema qualification |
| Session preparation | Aborts superseded preparation; list errors visible; native integrity failure visible | Initial selected session can trigger material fetch concurrently with prepare; backend single-worker serialization currently prevents mutation races |
| History | Invalid input/busy/error states; capabilities disabled natively | Query/freeze replies lack cancellation/selection guard; stale draft response can reinstate old result; no production history API yet |
| Catalogue/species | Loading/status exposed; sampled-limit notice; missing-name fallback to IDs | Catalogue error and genuinely empty species states lack clear retry/explanation |
| Studio voice | Unsupported library, loading/error getVoice, save conflict notice; per-knob error | Voice identity race repaired; accepted gestures close on original voice. Blank library error/empty and undo/edit reconciliation remain |
| Regions/mix | Unsupported/error query; mutations acknowledge some toggles | No complete loading/empty/error state; send/local policy restoration and concurrent mutation guards incomplete |
| Render/audition | Buttons gated; completed/failed/cancelled text; audition result message | Pending/queued gating, cancel errors/capability, polling and in-memory cross-screen job identity repaired; backend epoch/terminal conflict/reconnect semantics remain |

## Cross-screen state and persistence

Screen navigation changes only `screen`; providers, live cursor, bounded material stores, selected event/species/voice/region, replay session/speed, transport, map mode/orientation and reduced motion remain above the router. Material-mode changes deliberately clear event selection and change time basis. This is in-memory continuity, not saved-session/UI recall or browser-reload persistence.

Observed render job and pending-start state now survive navigation in shared UI state; job status is re-polled on Studio remount when supported. This is not durable render recovery. Screen-local state still destroyed on navigation: VoiceEditor undo/redo and dirty-ID decorations, local error/busy state, Atlas selected/incoming tab, map scope and species search. Unsaved canonical voice state belongs to the future host, so frontend must reload it rather than claim the view's undo stack is durable. An evicted selected event remains an ID but no longer resolves in its store; an explicit “no longer loaded” explanation would be clearer than silently treating it as never selected.

## Implementation order and evidence gates

This slice preserves H2 and repairs the four listed display/retry/identity/job defects with disposable behavioural/browser regressions. It does not advertise new capabilities. Next, qualify the remaining acknowledged gaps against explicit backend semantics. Promote native-supported provisional read DTOs only through an agreed shared schema; future engine work must define typed parameter ranges/units/reset/inheritance, gesture ack/undo, generation-aware transport, render job/file identity and cancellation before controls are enabled. Build integration readiness from real boundaries rather than dead scaffolding.

Current audit has not run owner data, owner collector, live-provider requests, native webview, sound playback, DAW or screen-reader/GPU qualification. Existing browser/typecheck/unit evidence belongs to its named checkpoint and does not prove the unimplemented backend. Proposed acceptance: deterministic delayed/error/unsupported fake hosts to exercise frontend branches, disposable native fixture for actual read methods, then later real native callback/device and owner listening as separate gates. No listening or quality acceptance is inferred from UI tests.

## Checks for this repair slice

Strict TypeScript and all 13 existing client/store/map unit tests pass. Native and demo production builds plus separation/CSP/licence/lazy-globe artifact checks pass. The existing native/demo browser suite passes nine groups using only new `build/frontend-readiness-fixture.sqlite3`. The added `ui/app/tests/integration-readiness-browser.mjs` passes eight actual-component groups using an injected, explicitly synthetic host: first/later page retry, GPS/receiver display, reordered voice operations, accepted gesture closure, queued/pending render gates, cancellation errors, polling without push, cross-screen job retention, stale progress and cross-job cancellation. It sends zero native bridge requests and reports zero page errors. No audio is generated by that host.

Run the new probes against local Vite with `PLAYWRIGHT_MODULE` pointing to an existing Playwright install; `ATLAS_TEST_URL` changes the loopback origin. Optional `ATLAS_READINESS_CASE` selects one documented scenario for targeted failure reproduction. Results and exact source hashes are recorded separately in `evidence/m2-audio/frontend-readiness-checks.json`; prior globe evidence is unchanged. Browser regressions used Chrome, not a native webview. No owner library/collector/provider request, DAW, real callback or subjective audio acceptance was exercised.
