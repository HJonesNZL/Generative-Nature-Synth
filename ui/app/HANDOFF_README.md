# Animal Atlas — UI handoff (Direction H2 · Region Mix)

Editable React + TypeScript source for the two standalone screens:

- **Atlas**: globe + New Zealand loupe, species highlight, region mix, event inspector, live/replay/history, and the coverage-aware history workflow.
- **Sound Studio**: voice library, voice definition editor, mappings, region effects, audition and render.

The build is static HTML/JS/CSS plus local assets, for JUCE's embedded browser.

The UI talks to the host only through one injected adapter (`src/host/client.ts` wrapping an `AtlasHost`). The proposed contract `src/host/contracts.ts` is copied **unchanged** from `ui/contracts.ts`. Methods that the UI needs but the contract lacks are in `src/host/provisional.ts`; each is listed in `docs/INTEGRATION_GAPS.md`. Running in a browser uses `src/host/mockHost.ts`, a **deterministic demo** adapter. It is visibly labelled DEMO, makes no network requests and produces no audio.

> **Status:** Atlas implements the approved H2 canvas design. Sound Studio is a **first pass in the H2 language that has not been reviewed as a canvas design**. All wildlife records, station positions, scores and counts are illustrative demo data.

## Requirements (tested)

| Tool | Version used |
|---|---|
| Node.js | 22.23.4 |
| npm | 10.9.9 |
| TypeScript | 5.9.3 (exact, dev dependency) |
| Vite | 8.3.5 (exact, dev dependency) |

All dependency versions are pinned exactly in `package.json` and `package-lock.json`. Runtime dependencies are React 19.3.1, React DOM 19.3.1 and three.js 0.187.0, all MIT licensed. Node is only needed to build. The installed application needs no Node, dev server, CDN or network.

## Commands

```sh
npm ci                # clean install from package-lock.json
npm run dev           # dev server at http://localhost:5173 (mock host)
npm run typecheck     # tsc --noEmit (strict)
npm run build         # typecheck + production build into dist/
npm run preview       # serve dist/ at http://localhost:4173 to check the build
```

## Output and resource assumptions for the native host

- `vite.config.ts` uses `base: "./"`, so every asset URL in `dist/` is **relative**. Serve `dist/` from a JUCE `WebBrowserComponent` resource provider (or any custom scheme) rooted at `dist/index.html`.
- There is **no client-side router** and no history-API fallback. The two screens are internal state, so only `index.html` must resolve.
- Fonts, the paper-grain texture, icons and Natural Earth coastline geometry are bundled. Nothing loads from the internet.
- `dist/assets/GlobeView-*.js` (three.js, ≈570 kB minified / 140 kB gzip) is loaded only when the globe is shown.
- The app does not use `localStorage`, cookies, service workers, `eval` or remote fetches. A suggested Content-Security-Policy for the host is in `docs/INTEGRATION_GAPS.md`.
- Source maps are emitted (`*.map`). Strip them from a release package if wanted.

## Connecting the native host (smallest steps)

1. **Write `src/host/nativeHost.ts`.** Implement `request(req, signal)` (the `AtlasHost` shape) over JUCE's native function/event bridge.
   - Resolve with the matching `HostReply`. Never throw for host errors; return `{ ok: false, error }`.
   - If it can push notifications, implement `subscribe(listener)` with the `HostNotification` shapes in `provisional.ts`.
2. **Create `src/main.native.tsx`.** Copy `main.tsx`, inject `new NativeAtlasHost()` and drop the `DemoMenu` import. Point `index.html` at it (or add a second HTML entry). The mock and demo menu then never ship.
3. **Return accurate `getCapabilities().supportedMethods`.** Every control whose method is absent renders disabled or "not offered". No other UI change is needed.
4. **Work through `docs/INTEGRATION_GAPS.md` in order.** Start with the subscription channel, `getCatalog`, `getMusicalOutcomes` and `getRegionActivity`. Those four make the Atlas truthful.
5. **Validate in the actual webview.** Use WKWebView on macOS and WebView2 on Windows, then fill in the unrun rows of `docs/VALIDATION.md`.

## File map

```text
index.html, vite.config.ts, tsconfig.json, package.json, package-lock.json
src/
  main.tsx                    browser/demo entry (mock host + demo menu)
  app/                        shell, shared state, header, feeds, hooks
    App.tsx                   providers, banners, screen switch
    HostContext.tsx           single HostClient, connection state, useHostQuery
    store.tsx                 shared selection/navigation/transport view state
    eventStore.ts             bounded event buffer outside React (batched per frame)
    useFeeds.ts               live cursor feed + replay/history material feed
    useCoalescedGesture.ts    begin/change/end coalescing + echo guard
    Header.tsx                nav, material mode, transport, collection, output
    catalog.ts, time.ts, units.ts
  screens/
    atlas/                    AtlasScreen, LeftColumn, MapStage, RightColumn (H2 Region Mix)
    studio/                   SoundStudioScreen, VoiceLibrary, VoiceEditor, RegionEffectsPanel
    shared/                   GrainPanel (bottom panel), useMaterialView
  components/                 Knob, SegmentedControl, Toggle, Popover, Notice, Icon
    map/                      GlobeView (three.js), Map2D (SVG), landTexture, geo
  host/
    contracts.ts              supplied proposed contract (unchanged)
    provisional.ts            PROVISIONAL methods + notifications (not in contract)
    client.ts                 HostClient: capability checks, ids, timeouts, abort
    mockHost.ts, mockData.ts  DEMO-ONLY deterministic adapter and data
    DemoMenu.tsx              DEMO-ONLY scenario switcher
  styles/                     tokens.css, fonts.css, global.css, app.css
  assets/
    fonts/                    Unbounded, Red Hat Text, Red Hat Mono (woff2, OFL) + licences
    icons/                    29 editable SVG icons (currentColor)
    geo/                      Natural Earth land (110m) + NZ (50m) rings
    textures/grain-light.png  paper grain (generated for this project)
docs/                         DESIGN_SPEC, INTERACTIONS_AND_STATES, CONTROL_INVENTORY,
                              INTEGRATION_GAPS, ASSET_LICENSES, VALIDATION
previews/                     PNG screenshots of screens, sizes and states
```

There is no `public/` folder. Every asset is imported from `src/assets/` and fingerprinted by Vite.
