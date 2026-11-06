# Animal Atlas UI

The supplied H2 Atlas and Sound Studio design is integrated as React/TypeScript with a lazy Three.js globe and SVG fallback. The default entry reads an existing library through the portable C++ host. The separate demo entry illustrates future sound-design interactions without producing audio.

## Open on this Mac

Double-click **Start Atlas UI.command** in the project root, then open http://127.0.0.1:5173/. It reads the existing `.state/atlas.sqlite3` without changing collection. When built, `build/native-playback/atlas-audio-host` supplies saved-piece native playback; otherwise `build/native/atlas-native` supplies the data preview. Audio starts disabled: select a saved piece, enable Listening, then press Play. Every piece, speed, basis or material-mode change stops the prior sound and requires another explicit enable. It neither starts nor changes collection. Keep that Terminal window open; Control-C closes only the UI preview. An already-running collector can continue recording independently. If the port is occupied, close the other UI preview first; the launcher does not kill any process.

Voice definitions are saved separately from the read-only wildlife library. The launcher exports `ATLAS_VOICE_LIBRARY`, defaulting to `.state/voices.sqlite3`; the native audio host owns its drafts and durable revisions. The data-only host continues to omit unsupported voice capabilities.

The launcher uses Node on PATH or this machine's existing Codex bundled Node. There is no automatic install, paid service, API key or runtime CDN. This is a development preview, not an installed JUCE standalone or VST3.

For the complete interactive **illustrative demo**, open http://127.0.0.1:5173/demo.html. The DEMO badge stays visible. All sound and collection controls there are simulated and do not affect your library. Demo edits are memory-only and disappear on reload.

## Portable development setup

Use Node compatible with the pinned Vite version (qualified here with Node 24.22.1). From this directory:

```sh
npm ci --ignore-scripts --no-audit --no-fund
npm run typecheck
npm test
ATLAS_LIBRARY=/absolute/path/to/existing/library.sqlite3 npm run dev
```

First build the repository's C++ target. On this Mac, use the Homebrew SQLite selection in the native architecture notes; Apple's older runtime failed the existing closed-WAL tests. `ATLAS_NATIVE_BINARY` can select a different absolute executable path; `ATLAS_AUDIO_HOST_BINARY` explicitly selects an audio host and takes precedence. Neither executable path is accepted from the browser. The browser polls native playback state; a bounded native lease stops and disables output if browser heartbeats disappear. Closing the server closes its child. Background throttling can expire that lease and requires manual re-enable; background playback is not qualified. Use one controlling UI window: multiple tabs share native preparation and the process-level lease.

```sh
npm run build
npm run build:demo
node tests/build-checks.mjs
```

`dist/` requires an injected `window.atlasHost`; without it the UI reports host unavailable and does not substitute demo data. `dist-demo/demo.html` is a separate, self-contained mock build. Both include local fonts, coastlines and notices under `licenses/`; neither includes the development HTTP transport. Do not open bundles via `file://`; serve them locally or use the future qualified webview origin.

## Implemented native boundary

Capabilities, latest arrival/correction polling, species/station catalogue, saved-piece listing, integrity-verified timeline preparation, and bounded saved event views. Historical requested windows—including empty hours—retain their original bounds. All microsecond timestamps, cursor sequences and generations cross the bridge as strings. Time labels currently use Pacific/Auckland.

The globe defaults to all loaded records, with a current-window toggle; the 2D fallback covers the world. Species search includes catalogue species and species IDs present in loaded records. All available species resets emphasis. An accessible location list shares the map selection. Stations aggregate detections at receivers; only GPS/satellite fixes mark measured animal positions, with no inferred paths. The newest loaded observation timestamp, scope counts and limitations stay visible. The map displays at most 400 markers, prioritising the selected location and highlighted species; it reports displayed versus available location symbols. The former 40-fix subset is removed; fixes use the selected map scope. The event view holds 2,000 records; omitted saved records are explicitly reported and remain intact in the library. Initial live connection baselines the newest 200 revisions without pulsing, then follows new revisions every two seconds while visible. Baselines/corrections never trigger arrival pulses. The catalogue samples 2,000 records and lists up to 500 species/stations and 128 sources; saved-piece listing is the newest 100. Grain shows at most six species lanes. These are display limits, not collection or musical admission policies.

The optional native audio host adds host-owned voice editing and saving (see [native voices](../../docs/milestones/UI_NATIVE_VOICES.md)), plus generation-fenced saved-piece preparation, audio enable, Play/Pause/Stop/Return to start/All off and polled native timeline position including tails. The master slider remains disabled; fixed output attenuation is −18 dB. Output meter and active voice count are unavailable rather than fabricated. The data-only host still disables all audio controls.

Native scene composition now exposes full key/scale/tempo/density/variation/seed/timing settings, and Sound Studio adds up to eight ordered user-authored geographic regions with bounds, pan/gain and echo/room ambience. First matching region wins; reported station positions remain receiver observations, while missing/outside positions are unassigned. These are artistic geographic selections, without habitat or track inference. Each explicit Apply validates its draft, quietly re-prepares the current piece and preserves the other editor's pending changes. Save soundscape waits for voice gestures, composition and spatial acknowledgements in order; recall restores exact saved controls with Listening off. See [composition UI](../../docs/milestones/COMPOSITION_UI.md) and [spatial UI](../../docs/milestones/SPATIAL_UI.md).

When the host advertises scene storage/rendering, the Soundscapes controls capture copied events, voices, exact speed, composition and ordered spatial settings. Export saved WAV renders the selected immutable soundscape even after later draft edits. The launcher configures app-owned `.state/soundscapes` and `Exports` directories through `ATLAS_SOUNDSCAPE_DIRECTORY` and `ATLAS_EXPORT_DIRECTORY`. Browser clients receive opaque scene/artifact IDs, without filesystem paths.

Not exposed by either host: live sonification, history search/freezing, collection control, source-health reporting, production host notifications or durable UI layout settings. The native view disables unavailable controls. Original delivery proposals remain design references; the implemented DSP behavior is defined by the current milestone contracts.

## Verification

`tests/boundaries.test.mjs` covers capability/correlation checks, cancellation/timeouts, revision deduplication, bounded displays and stale outcomes. `tests/build-checks.mjs` checks bundle separation and notices after both builds.

For browser integration, create a disposable library from the repository root:

```sh
python3 -B tests/ui_fixture.py build/ui-browser-fixture.sqlite3
```

The script refuses to replace an existing file. Start Vite with that library (absolute path), then from this directory run `node tests/browser-checks.mjs`. It requires Playwright and an installed Chrome, used only for development tests. Set `PLAYWRIGHT_MODULE` to an existing module path when it is not on normal module resolution, and `ATLAS_TEST_URL`/`ATLAS_BROWSER` to override the defaults. Never run this fixture workflow against the owner database. Results/screenshots go to ignored `test-results/`.

For static browser checks, serve `dist` with `npm run preview` and `dist-demo` with `npm run preview -- --outDir dist-demo --port 4174`, then run `node tests/static-browser-checks.mjs` with the same Playwright setup.

See [integration evidence](../../docs/milestones/UI_HANDOFF_INTEGRATION.md). Original delivery docs are preserved in `HANDOFF_README.md` and `docs/`; their claims are the designer's record, not native qualification.


Saved-piece playback contracts, safety ownership and current evidence: [UI native playback](../../docs/milestones/UI_NATIVE_PLAYBACK.md). `tests/playback-browser.mjs` checks actual React controls against an explicitly silent synthetic host with deferred/error replies. `tests/native-playback-browser.mjs` requires the native `atlas-playback-fixture-host` and a new disposable synthetic library; it exercises the real C++ boundary/runtime/DSP through a silent test output driver. Neither proves physical-device output or subjective sound quality. `tests/native-bridge.test.mjs` covers origin/envelope bounds, queue saturation, cancellation, timeout, child replacement and server teardown.

Composition and spatial integration use `tests/native-soundscapes-browser.mjs` with `ATLAS_TEST_COMPOSITION=1` and `tests/native-spatial-browser.mjs`, respectively. They create disposable synthetic databases and use `atlas-studio-fixture-host` with silent output. The spatial check includes independent Apply draft preservation, held acknowledgements for both recipes during Save, dateline/ordered regions, exact recall, stereo WAV differences, immutable exports and same-piece external-window conflict rejection. Evidence: [composition validation](../../evidence/composition/ui-validation.json), [spatial validation](../../evidence/spatial/ui-validation.json). These checks do not establish physical audio/DAW output or listening approval.
