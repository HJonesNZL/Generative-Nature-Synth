# Prompt to send to the Claude design agent

Please prepare your current Generative Nature Synth / Animal Atlas design for engineering handoff. Preserve the design direction we have developed. Package editable implementation files and explain the interactions sufficiently for a C++ audio engineer to integrate the UI.

## Product and integration context

The product has two primary screens: **Atlas**, for geography, animal detections/tracks, incoming activity and historical selection; and **Sound Studio**, for species/known-individual voices, mappings and geographic effects. They share the selected piece, event/species/region, transport and editing state. Historical composition selects species, region and a point/range in time where records actually exist. Live, replay, history, imported and demo contexts must be distinguishable.

Production collection, sessions, musical timing, synthesis and DSP are portable C++20. The recommended shell is JUCE standalone, then VST3, with an embedded webview. React and a webview are complementary: **deliver React + TypeScript source that builds into locally served static HTML/JS/CSS/assets**. Three.js/WebGL is acceptable for the globe. JUCE and its bridge are not implemented yet; the attached `ui/contracts.ts` is a proposed integration contract, not a working API. Backend work is proceeding on `beta-1` in https://github.com/HJonesNZL/Generative-Nature-Synth.

Everything required to run the software must be zero cost. No paid assets/fonts/map tiles, API subscriptions, hosted backend, accounts, CDN or required online resources. A free development build tool is fine. The installed application must not need Node, a development server or network access to display the interface.

## Required delivery

Provide one downloadable ZIP containing a complete editable project. Prefer Vite with React and TypeScript unless the existing design implementation has a justified alternative that still produces a static build. Include:

```text
animal-atlas-ui-handoff/
  README.md
  package.json
  package-lock.json              # or the matching lockfile for the chosen manager
  index.html
  tsconfig*.json
  vite.config.ts                # if using Vite
  src/
    app/                        # shell and shared selection/navigation
    screens/                    # Atlas and Sound Studio
    components/                 # reusable controls
    host/
      contracts.ts              # supplied proposed contract
      mockHost.ts               # clearly labelled deterministic demo
    styles/                     # tokens, global styles and component styling
    assets/                     # local assets, or use public/ consistently
  public/                       # any statically copied assets
  docs/
    DESIGN_SPEC.md
    INTERACTIONS_AND_STATES.md
    CONTROL_INVENTORY.md
    INTEGRATION_GAPS.md
    ASSET_LICENSES.md
    VALIDATION.md
  previews/                     # named PNG screenshots; optional motion video
```

The layout is a guide; retain a clean existing structure rather than rewriting solely to match it. Include every referenced source/configuration/asset file. Exclude node_modules, caches, credentials and temporary output. A `dist/` preview is optional and must accompany editable source. A hosted preview URL, screenshots, flattened canvas, Figma file or single image alone is insufficient. If your export system cannot produce some files, list precisely what is absent and supply the closest editable source; do not claim a complete runnable package.

## Implementation boundary

Keep native communication behind one injected `AtlasHost` adapter. Components should not directly depend on JUCE globals or a specific IPC transport. Use the supplied typed contract where it fits. If it is unavailable, define a small clearly provisional adapter and list differences for the engineer to reconcile.

The prototype may use a deterministic mock adapter, visibly marked Demo. Do not fetch wildlife sources, access SQLite, create a cloud backend, expose credentials, read arbitrary filesystem paths or implement a second audio engine. The C++ host will own audio, sample-accurate timing, collection, canonical session state, file dialogs and rendering. A moving waveform/meter in the mock must not imply actual audio processing.

Check host capabilities before offering actions. The existing contract is incomplete: session save/freeze, region editing, presets, audio-device selection, render/export, cancellation/progress and native state subscriptions need further design. Document required additions in INTEGRATION_GAPS.md with request/result/error examples; do not silently present them as existing methods. Keep mock-only extensions identifiable.

Use string IDs for 64-bit sequence numbers and microsecond values. Display timestamps with an explicit timezone; preserve UTC values. Await native acknowledgement for authoritative changes; handle failures and stale responses. Coalesce parameter drags and expose begin/change/end gestures. Avoid feedback loops when native automation updates a control. Switching screens or reconnecting the view must not retrigger events or reset the piece.

## Design specification and control inventory

Document the final typography, spacing, colours, panel dimensions, borders, shadows, icon sizes, motion durations, responsive behaviour and semantic design tokens. Supply editable SVG icons and appropriate raster/3D source assets, with names that match their use. Include all required font files where redistribution is permitted and a fallback stack. Record origin, licence/permission and modifications for every external asset, font and dependency. Flag unresolved assets for replacement; do not embed reference-image company branding or treat generated map imagery as accurate geography.

For each functional control, document its purpose, displayed label, unit/range/default, edit interaction, reset behaviour, disabled state and intended host command/state. Mark suggested DSP ranges as proposals for engineering review. Distinguish a saved species voice definition from a currently sounding synth voice. Explain group/species/individual overrides, region overlap, dry/wet sends, mute/solo, panic and tail behaviour where shown; identify unresolved choices rather than inventing backend behaviour.

Show the main user journeys and state transitions: event selection → matching voice; historical query → coverage preview → freeze; voice/region edit → save; reopen → play/render. Explain dirty state, undo/redo scope and save/discard/cancel. Collection, recording, playback and audio enable are separate states and controls.

## Essential truthful states

Include empty, loading, unavailable, offline, stale, reconnecting, partial results, unknown coverage, truncated capture, no matching records, missing/corrupt piece, audio unavailable/device lost, render progress/cancel/failure and unsupported capability states. A microphone detection is not an identified animal journey. Unknown location/confidence remains unknown. No observations is not proof of ecological silence. Show requested historical interval separately from actual material and distinguish preserving that interval from trimming playback to events. Mock activity and illustrative statistics must stay labelled.

## Usability and rendering

Provide keyboard navigation, visible focus, labels/roles/values, numeric entry for knobs, appropriate fine adjustment, non-colour status indicators, accessible errors and reduced-motion behaviour. Include a usable event list/inspector and 2D fallback so the globe is not the sole way to select material. Aim for WCAG 2.2 AA controls; report checks actually performed without claiming full conformance from screenshots.

Declare the smallest usable window. Supply previews at that size and representative 1440×900 and 1920×1080 desktop layouts, plus one scaled/text-enlarged case. The future compact VST3 editor can be a separate annotated proposal; it need not delay the two primary standalone screens.

Use bounded marker/label/pulse pools, batched updates and a declared animation cap. Pause hidden-page/editor animation, dispose GPU resources on teardown, handle WebGL context loss and avoid large per-event React rerenders. Keep the essential controls usable if graphics fail. Do not use browser timers as musical transport or block the UI while waiting for a native operation.

## Validation and delivery summary

README must specify the tested Node/package-manager versions and exact clean-install, dev, typecheck and production-build commands. Use relative/resource-safe asset URLs and document any routing/resource assumptions the native host must supply. Prefer no browser-history server fallback dependency for this two-screen shell.

In VALIDATION.md record actual build/typecheck results, browsers tested, keyboard/focus checks, reduced-motion behaviour, narrow-window results, known limitations and any unrun checks. Preview both screens and the major failure/empty/history states. Do not claim JUCE, embedded-webview, audio or DAW validation unless actually tested there.

Finish with: the ZIP; a brief file map; implemented versus illustrative interactions; proposed API additions; unresolved design/asset decisions; and the smallest steps needed to connect the real native host. Keep the implementation understandable: no unnecessary dependencies, generic dashboard framework or speculative abstraction.
