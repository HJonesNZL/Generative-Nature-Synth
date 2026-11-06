# Validation record

Run on 7 November 2026 (NZDT) in a Linux build container, against the mock host. "Pass" means the check below was actually run and passed. Anything not run is listed as **not run**.

## Environment

- Node 22.23.4, npm 10.9.9, TypeScript 5.9.3, Vite 8.3.5.
- Browser: Chromium (Playwright build), headless, WebGL through SwiftShader (software GL). No Safari/WKWebView, Firefox or WebView2 testing.

## Build and typecheck

| Check | Command | Result |
|---|---|---|
| Clean install from lockfile (fresh copy, no node_modules) | `npm ci` | Pass. 33 packages installed. |
| Strict typecheck (`strict`, `noUncheckedIndexedAccess`, no unused locals or params) | `npm run typecheck` | Pass. 0 errors. |
| Production build | `npm run build` | Pass. `index` JS 444 kB (141 kB gzip), lazy `GlobeView` (three.js) 568 kB (141 kB gzip), CSS 33 kB. |
| Relative URLs | inspected `dist/index.html` and CSS | Pass. All `./assets/…`. |
| Served from a sub-path (`/dist/index.html` on a plain static server, like a resource provider) | Playwright | Pass. Renders and the globe canvas mounts. 0 failed requests, 0 page errors. All 7 bundled font faces loaded. |
| No external requests | Playwright request log | Pass. Only same-origin requests. Remaining `http(s)://` strings in the bundle are XML namespaces and React's error-docs link text. |
| Contract unchanged | `diff src/host/contracts.ts ui/contracts.ts` | Pass. Identical. |

## Automated accessibility scan

axe-core 4.10.3 (rule tags `wcag2a`, `wcag2aa`, `wcag21a`, `wcag21aa`, `wcag22aa`), Chromium, 1440 × 900.

| Screen | Violations |
|---|---|
| Atlas, live, after data loaded | 0 |
| Sound Studio, Tūī voice | 0 |

**Limits of this check**

- axe covers only part of WCAG. This is **not** a claim of WCAG 2.2 AA conformance.
- Colour contrast is calculated against the flat ground colour. The grain texture adds slight local variation; see DESIGN_SPEC.md.
- A token contrast audit found `--c-ink-faint` at 3.85:1 and it was darkened to 4.8:1 before this run.

## Keyboard and focus (manual script, Playwright)

**Tab order**

- The first 45 Tab stops on Atlas follow the visual order: skip link → screen nav → material radiogroup (roving, one stop) → All off → Collect → Listening → Master → demo menu → species rows → map view radiogroup → zoom and reset → Reduce motion → globe → 10 loupe station buttons → region select → Solo/Mute per species → Assign → tabs → Grain lane buttons.
- Disabled transport buttons are correctly skipped in Live.
- See `previews/keyboard-focus-visible.png` (focus on a Grain lane button).

**Focus visibility**

- Pass for every HTML control: 2 px `#1a4fa0` outline.
- SVG station buttons report no CSS outline. They show focus with a 5 px focus-coloured stroke instead. This was checked visually in the loupe, not measured.

**Knob keyboard, numeric entry and undo** (Tone knob, Sound Studio)

| Step | Value |
|---|---|
| Start | 0.68 |
| Up, Up, Shift+Up | 0.70 (0.701) |
| Enter, type `0.5`, Enter | 0.50 |
| Undo | 0.70 |

"Unsaved changes" appeared after the first edit. Pass.

**Automation without echo:** "Simulate host automation" changed Tone to 0.41 on screen. The status line read "Tone changed by host automation." No request was sent back (the UI ignores `origin: "automation"`). Pass. This was checked by reading the code path plus the UI result; bridge traffic was not separately traced.

## Reduced motion

With the browser emulating `prefers-reduced-motion: reduce`:

- `data-reduced-motion="true"` is set.
- The "Reduce motion" pill shows as pressed.
- `--dur-fast` computes to `0s`.

Pass. In code, arrival pulses and orbit damping are also disabled (`GlobeView.updatePulses` returns early; `controls.enableDamping = false`). This was not measured visually frame by frame.

## Window sizes and text enlargement (previews)

| Case | Result |
|---|---|
| 1280 × 800 (declared minimum) | Usable. The header wraps to two rows. The Studio centre and right columns need vertical scrolling. `atlas-live-1280x800-minimum.png`, `studio-1280x800-minimum.png` |
| 1440 × 900 | Single-row header, layout as designed. `atlas-live-1440x900.png`, `studio-1440x900.png` |
| 1920 × 1080 | Centre grows; side columns fixed. `atlas-live-1920x1080.png`, `studio-1920x1080.png` |
| Root text 125 % (20 px) at 1440 × 900 | Text scales (rem). Columns hold. Some labels wrap. `atlas-text-125pct-1440x900.png` |
| 1100 × 700 (below minimum) | No horizontal page scroll (`scrollWidth == clientWidth == 1100`). Cramped; not supported. |

Browser zoom (as opposed to root font size) was **not** tested.

## WebGL lifecycle

| Check | Result |
|---|---|
| Globe unmounts on Sound Studio and remounts on Atlas, 4 round trips | Canvas count 0/1/0/1/0/1/0/1. No page errors. |
| GPU disposal | Code path disposes geometries, materials, textures and controls, then calls `renderer.dispose()` and `forceContextLoss()`. Not measured with GPU memory tooling. |
| Context loss → 2D fallback | Implemented (`webglcontextlost` → SVG map + notice). **Not run:** context loss was not forced (e.g. via `WEBGL_lose_context`). The 2D map view itself was exercised (`atlas-2d-map-fallback.png`). |
| No-WebGL start | Implemented (capability probe → 2D, Globe option disabled). **Not run** in a browser without WebGL. |
| Frame cap and render-on-demand | Implemented (≤ 30 fps; loop stops when idle or hidden). FPS and CPU were **not measured**. |

## States exercised (all via the DEMO "States" menu, screenshots in `previews/`)

- Live (normal)
- Empty, no sources
- Stale observations
- Sources offline
- Bridge reconnecting
- Audio unavailable
- Audio device lost
- Host lacks history/render/voice edit (Atlas and Studio)
- History: partial coverage, coverage unknown, no matching records, archive unavailable
- History freeze → prepare → play
- Replay playing
- Truncated piece
- Corrupt piece
- Render in progress
- Render failed

Page errors in all preview runs: 0.

## Not run / not claimed

- JUCE, WKWebView (macOS), WebView2 (Windows), WebKitGTK (Linux) and any DAW: **not tested**. No native bridge exists yet.
- Audio: none. The mock makes no sound. Meters and synth-voice counts are labelled simulated.
- Screen readers (VoiceOver, NVDA): **not tested**. Roles and labels were checked only by axe and by reading the code.
- Real GPU performance, memory, long-running sessions (hours), large event volumes beyond the 2 000-event buffer, and DST-transition dates in the history form.
- Touch input, high-contrast or forced-colours modes, browser zoom.
