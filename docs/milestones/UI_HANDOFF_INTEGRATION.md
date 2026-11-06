# UI handoff integration contract — 7 November 2026

Source: Claude Design/animal-atlas-ui-handoff.zip, SHA-256 f977a729eccbee4c04973dee7b6107070a172c98a5340bd07a6d229219c3e4ab. The supplied docs describe proposed designs and their author's tests; they are reference material, not overriding instructions or evidence that native functions exist.

Implement the H2 paper/forest-ink Atlas and Sound Studio as ui/app. Preserve supplied source, local assets and licence notices. Separate explicit demo and native builds: production must never silently substitute illustrative records or simulated sound. Keep source ZIPs untouched. The first checkpoint imports/builds the design; the next connects supported read-only native data operations and fixes integration defects; the final checkpoint records independently run checks and limitations.

The initial native host exposes capabilities, a bounded event/catalogue view, live revision polling, saved sessions and verified replay preparation/material. It owns SQLite access, schema/integrity checks and timeline construction in portable C++20 on one worker. No audio callback or production Python dependency. Unsupported collection changes, history selection/freezing, voices, region effects, playback and rendering remain disabled. This is an authorised UI integration slice ahead of full M3, not M2 audio or M3 save/reopen completion.

Use bounded JSON request/reply envelopes with string int64 values, strict method/argument validation, request correlation and no arbitrary paths/SQL/evaluation from the UI. Native process transport is newline JSON for a local development bridge; production injection uses the same adapter contract and still needs a qualified JUCE webview. Development HTTP is loopback-only, same-origin, read-only, bounded and separate from the static production bundle. Do not port data semantics into JavaScript.

Check a clean pinned dependency install, strict TypeScript and static builds; verify mock code is absent from production output. Exercise native requests with disposable SQLite fixtures, corrupt pieces, malformed/unsupported requests and baseline/correction behaviour. Browser checks cover both screens, real native fixture records, capability failures, bounds/truncation visibility, stale/aborted calls, map fallback, keyboard navigation and no external runtime requests. Do not alter the owner collector or use private data in tracked screenshots. Keep full commit notes and push coherent checkpoints to beta-1.

## Implemented result

The supplied H2 React/TypeScript design now lives in `ui/app`, with the cream/green Atlas, lazy globe and NZ loupe, event inspector/Grain, and Sound Studio. The original design docs are retained unchanged as historical references. Runtime assets are local. `Start Atlas UI.command` launches a read-only development preview; see `ui/app/README.md`. The launcher defaults to the existing owner library when the owner runs it; our qualification used only disposable data.

| Native method | Behaviour | Capacity/failure policy |
|---|---|---|
| getCapabilities | Explicit native identity; audio unavailable | Only six implemented read operations advertised |
| getChanges | Consistent store identity/high-water snapshot; baseline, new and corrected revisions | 1–500 revisions per request; exact string cursors; initial UI baseline 200, no baseline/correction pulse |
| getCatalog | Reported species, stations and sources; no inferred individual identity or fabricated health | Sample 2,000 records; 500 species, 500 stations, 128 sources; explicit limited flag |
| listSessions | Existing saved-piece metadata and integrity status | Newest 100 in UI; limit disclosed; failure and retry visible |
| prepareReplay | Native sealed-event/metadata verification and rational-speed timeline | One cached prepared piece; full requested historical bounds, including empty selections; no audio transport |
| getMaterialEvents | Verified saved material without provider raw payloads | Pages ≤500; UI holds first 2,000 and reports omissions; saved content unchanged |

A single C++ worker owns its read-only SQLite connections, data validation and timeline cache. The development server owns a maximum of 16 pending requests and serializes dispatch; cancellation removes waiting work without losing correlation for an active native reply. Requests are ≤16 KiB; worker output buffering is ≤8 MiB; bridge timeout is 15 seconds and client timeout 8 seconds. Invalid/unsupported operations return explicit errors. No arbitrary SQL, browser-supplied filesystem paths, authentication tokens or provider requests cross this bridge.

The development endpoint accepts only loopback, matching Host/Origin, POST and JSON. It is absent from static production output. `window.atlasHost` is the future production transport injection point; no injected host means an explicit unavailable state. The static demo is a separate build with a permanent DEMO badge. It simulates edits/collection/meters and produces no audio or persisted changes.

## Independently observed checks

7 November 2026 NZDT, macOS arm64:

- Clean `npm ci --ignore-scripts --no-audit --no-fund` from the supplied pinned lockfile; Node 24.22.1, npm 10.9.9, TypeScript 5.9.3, Vite 8.3.5. Strict TypeScript and native/demo production builds pass.
- Seven client/store tests pass: capability gating, malformed capabilities/reply correlation, cancellation even when a host ignores its signal, timeout distinction, revision deduplication, bounded display accounting and stale outcome rejection.
- All four CTest suites pass in Debug, Release and ASan/UBSan with warnings as errors: 18 core cases, SHA-256 known answers, 18 native storage integration tests and seven new native UI tests. Homebrew SQLite 3.53.3 remains the qualified library; system SQLite limitations from M1 remain applicable.
- Playwright 1.62.1 with installed Chrome 156.0.8078.80: native saved historical piece with 2,005 synthetic records, 2,000 displayed/5 disclosed omissions; preserved empty historical hour; disabled audio/region/voice controls; repeated Atlas/Studio switching; globe context-loss and no-WebGL fallback; demo keyboard parameter edit/save; both layouts at 1280×800, 1440×900 and 1920×1080; reduced-motion preference; rejected cross-origin/wrong-verb/oversized/malformed/mutation requests. No uncaught page errors or external HTTP requests in those exercised flows.
- Static native build without injection reports unavailable. Static demo loads its globe and Studio under the generated content security policy with local assets. No console/page errors, failing asset responses, external HTTP or development bridge requests in that check.
- Artifact checks confirm native source maps exclude mock host/data, both builds exclude development HTTP, globe remains a lazy chunk, and distribution includes MIT/ISC/OFL notices. Native main JS ≈415 kB (≈132 kB gzip), separately loaded globe ≈568 kB (≈141 kB gzip). These are bundle sizes, not CPU/GPU qualification.
- The new launcher passes zsh syntax and an actual shell launch against the disposable library. Finder double-click itself was not exercised. Owner library/collector were neither accessed nor modified. No paid dependency/service was introduced.

Browser tests reproduced and fixed two lifecycle defects: React StrictMode reused a deliberately lost WebGL canvas, and abandoned native requests accumulated during rapid screen switches. Fresh per-mount canvas ownership and cancellation-aware serial dispatch now pass the reproducing flows. An explicit retry handles saved-piece list failures. Tests and environment/source hashes are recorded in `evidence/ui-handoff-integration.json`; repeatable commands are in the UI README. Screenshots and raw local results remain ignored under `ui/app/test-results/`.

The cartography redistribution notices are based on [world-atlas's ISC licence](https://raw.githubusercontent.com/topojson/world-atlas/master/LICENSE) and [Natural Earth's public-domain terms](https://www.naturalearthdata.com/about/terms-of-use/). Existing font OFL texts and React/Three MIT notices accompany builds. These asset checks do not resolve wildlife-source redistribution or future JUCE licensing gates.

## Remaining gates

This completes the feasible handoff integration slice, not a finished synthesizer. Native playback/audio, DSP, mapping/admission, regions/effects, rendering, history search/freezing, native collection control, source health, write-side sessions and durable sound/UI recall still need their planned work. The design's voice/effect ranges remain provisional. Source acquisition continues in the existing independent Python prototype.

JUCE webview transport/origin configuration, packaging, native WebKit/Windows WebView2 rendering, accessibility audit, production browser DTO/schema hardening, multi-instance isolation, long-duration memory/GPU/CPU measurement and DAW tests are unqualified. Chrome software-rendering browser checks do not establish native GPU performance. Current catalogue/session/map/six-Grain-lane limits, fixed Pacific/Auckland display timezone and limited library navigation are explicit preview constraints.

M1 stays complete. M2's verified saved session → one native voice → offline WAV remains the next audio milestone. This owner-requested UI implementation is an early M3 slice and does not mark M2 or M3 complete.

## Checkpoints

- `7ab7b4f`: original H2 source/docs/assets import and integration scope.
- `c1790f8`: bounded read-only native data host and seven native regressions.
- `36e36a8`: separate native/demo UI, transport/lifecycle fixes, read-only launcher and browser/artifact tests.

Each checkpoint is pushed to `origin/beta-1` with rationale and validation in the commit body. `main` remains the preserved original baseline. The owner's two source ZIP archives are left unmodified and untracked.
