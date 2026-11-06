# Interactions and states

**Ownership of state:**

- **Host (C++)** owns: audio, sample-accurate timing, collection, canonical sessions, voices, regions, file dialogs and rendering.
- **UI** owns only: view selection, the current screen, temporary edit context (undo stack for unsaved edits) and display caches.

## 1. Shared state across screens

`src/app/store.tsx` holds the following above the screen switch, so switching Atlas ⇄ Sound Studio never resets them:

- screen and material mode (`live | replay | history`)
- time basis (`observed | arrival`)
- highlighted species, selected event, region and voice
- map view (globe / 2D) and globe orientation
- reduced-motion setting
- history draft, result and frozen piece
- replay session and speed
- transport view (generation, state, position, duration, preparation id)

The live cursor (`storeId` + `nextSequence`) lives in the live `EventStore`, which is also above the screen switch.

- **Switching screens** unmounts the globe (freeing its WebGL context) but does not touch the cursor. When the Atlas returns, the feed continues from where it was. Nothing is re-requested from `"0"` and nothing is re-pulsed.
- **Reconnecting the bridge** resumes from the same cursor.
- **A changed `storeId`** (host reset or a different library) clears the buffer and starts a fresh *baseline*.
- **Baseline and corrected records** never pulse and never count as new arrivals. Only records with `liveEligible && change === "new"` produce a map ping.

## 2. Material context (header)

| Mode | Meaning | Left column | Transport |
|---|---|---|---|
| Live | newly arriving records (arrival time by default) | species highlight + source health | Play/Stop disabled: live arrivals have no timeline. All off (panic) works. |
| Replay | a saved piece, played from a fixed prepared timeline | saved pieces, speed (1/10/60/240×), basis | Play/Pause/Stop/Return to start via `setTransport`, enabled after `prepareReplay` |
| History | archive material chosen by species, region and time | query → coverage → freeze | enabled after freeze + prepare |

**Provenance** is shown separately from mode:

- the DEMO badge in the header (mock host)
- an "Imported" badge on imported pieces
- "Demo record" / "Imported record" on the inspector

Replay never makes data look live, and live never makes demo data look real.

## 3. Separate controls: collection, recording, playback, listening

| Control | Where | Host method | Notes |
|---|---|---|---|
| **Collect** chip | header | `setCollection` (provisional) | 1/2/4 h, with or without "play while collecting". Stopping needs confirmation and keeps all records. Independent of playback; runs without the window open. |
| **Play / Pause / Stop / Return to start** | header | `setTransport` | Only with a prepared piece. Position comes from host `transportChanged` notifications; the UI never runs its own clock. |
| **All off** | header | `setTransport { action: "panic" }` | Always available when the host supports `setTransport`. Releases every note and tail; it does not stop collection or change selection. |
| **Listening** switch | header | `setAudioEnabled` (provisional) | Audio enable only. Disabled when the device is unavailable or lost. |
| **Master** slider | header | `setMasterGain` (provisional), gestures | −60…+6 dB, step 0.5. |

## 4. Main journeys

### 4.1 Event selection → matching voice

1. Select an event: click a station on the globe, a station in the loupe or 2D map (keyboard: Tab to it, Enter), a line in Grain (mouse), or a row under *Incoming* (keyboard).
2. The station picks its most recent event, preferring the highlighted species. The inspector shows kind, observed and arrived times (with explicit zone), identity, location meaning, confidence *with its provider meaning*, source and the musical outcome from `getMusicalOutcomes`.
3. Press **Open ⟨species⟩ voice in Sound Studio**. The UI sets `selectedVoiceId` (from the outcome's voice, else the library entry), highlights the species and switches screen. The selected event, map selection and transport are kept.
4. Species with no voice show **Assign a voice in Sound Studio**. Assignment itself needs a host method (gap G9).

### 4.2 Historical query → coverage preview → freeze → play

1. **History** mode: choose species (chips), region, date, from/to and timezone (New Zealand or UTC).
   - The window is converted to UTC with the explicit IANA zone (`zonedToUtc`; the DST-skipped hour is flagged as an edge case).
   - Validation runs inline: end must be after start, at least one species.
2. **Check available data** calls `queryHistory`. The result is shown as:
   - Requested interval vs minutes *with records*, *covered without detections*, and *missing/unknown*.
   - A status notice: **Partial**, **Coverage unknown** ("do not read silence here as absence of animals"), **Covered, no matching records**, or **Archive unavailable**.
   - The **coverage band** in Grain: solid = records, outline = covered/none, hatched = missing, dashed = unknown. Each has a text equivalent for screen readers.
3. **Freeze soundscape**: name it, then choose:
   - **Keep the requested interval (gaps stay silent)**, or
   - **Trim playback to first–last record**.

   This calls `freezeHistorySelection` (provisional). The result shows both the requested window and the material window.
4. The UI then calls `prepareReplay(frozenId)`; Play becomes available.
5. Editing the draft clears the result and the frozen reference, so a stale selection cannot be frozen.

### 4.3 Voice / region edit → save

1. In Sound Studio, select a group default, a species voice or an individual override in the library. Each row says where its sound comes from: *species voice*, *inherits group*, *override*, *inherits*, or *no voice*.
2. Edit: voice type (`setVoiceType`), knobs (`setVoiceParameter` with begin/change/end), mapping toggles (`setMappingEnabled`).
   - Every edit marks the definition **Unsaved changes**.
   - The library shows "• unsaved".
3. **Undo / Redo** covers committed gestures (max 50) on *this definition's unsaved edits in this view*. Undo re-sends the earlier value as a begin/change/end gesture. Save or Revert clears the stack, and so does switching voices (the host keeps the unsaved state; the dirty marker remains).
4. **Save voice** calls `saveVoice(expectedRevision)`. A `stale_generation` reply ("saved elsewhere") tells the user to Revert and reapply. **Revert** calls `revertVoice`.
5. **Region edits** (enable/bypass, send, overlap policy, uncertain-position policy) apply immediately via host acknowledgement. They are not part of the voice's dirty state. Whether region edits should be session-dirty is an open question (gap G12).

### 4.4 Reopen → play / render

1. **Replay** lists saved pieces (`listSessions`). Badges show Imported, Interrupted and Truncated.
2. Selecting a piece calls `prepareReplay` with the chosen speed and basis. Errors (e.g. integrity failure) are shown and nothing opens.
3. **Render WAV…** (Sound Studio context strip) calls `startRender` for the open piece or frozen history:
   - Progress, Cancel, complete and failed states come from `renderProgress` notifications.
   - Rendering never needs live data.

## 5. Gestures, acknowledgement and feedback loops

- **Knobs and sliders** send `begin`, coalesced `change` (one request in flight, latest value wins) and `end` (`useCoalescedGesture`).
- **Display during a drag:** the control shows the local value. Afterwards it shows the host-acknowledged value.
- **Echo suppression:** host replies carry `changeRevision`. Notifications at or below the last acknowledged revision, or with `origin: "ui"`, are ignored as our own echo.
- **Automation:** `voiceParameterChanged` with `origin: "automation" | "restore"` updates the display only. It is never re-sent, and a status line says so.
- **While dragging,** external values are not applied, so the control never jumps under the pointer.
- **Mute/Solo** waits for acknowledgement (`aria-busy` while pending). It then refreshes Region Mix and invalidates cached outcomes.
- **Stale responses:** `useHostQuery` aborts the previous request when its inputs change and applies only the latest reply. Transport replies with `stale_generation` ask the user to prepare again.
- **Timeouts:** 8 s by default (20 s for history queries). A timeout is shown as an error and is not retried automatically for mutations.

## 6. Truthful states (all reachable from the DEMO "States" menu)

| State | Where it shows | Wording / behaviour | Preview |
|---|---|---|---|
| Connecting | banner | "Connecting to the host…" | — |
| Reconnecting | banner + per-panel errors | "Controls that change the piece are paused; nothing will be replayed when it returns." Feed retries with backoff from its cursor. | `state-bridge-reconnecting.png` |
| Host unavailable (after 4 tries) | banner with Retry | — | — |
| No sources selected (empty) | left column | "Nothing will arrive until a source is enabled in source setup." | `state-empty-no-sources.png` |
| Connected but stale | source row | Connection and newest-observation age are separate lines; "stale" after 3 h. | `state-stale-observations.png` |
| Offline / retrying source | source row | glyph + text + error code; records already received stay visible | `state-sources-offline.png` |
| Quiet window | left column, Grain | "A quiet source stays quiet; nothing is invented." / "not proof the animals were silent" | — |
| Loading | panels | "Loading …" text, previous values kept while refreshing | — |
| Partial coverage | history | caution notice + coverage band | `atlas-history-coverage-partial.png` |
| Coverage unknown | history | caution notice; coverage band dashed; record count "unknown" | `state-history-unknown.png` |
| Covered, no matching records | history | info notice; freeze not offered | `state-history-no-matches.png` |
| Archive unavailable | history | problem notice; nothing selected | `state-history-unavailable.png` |
| Truncated capture | replay | Truncated badge + notice | `state-piece-truncated.png` |
| Interrupted capture | replay | Interrupted badge (visible in `atlas-replay-playing.png`); selecting it adds a notice ("no coverage is assumed after it") | `atlas-replay-playing.png` |
| Missing / corrupt piece | replay | "Could not open piece — Bundle failed integrity check… The piece was not opened." | `state-piece-corrupt.png` |
| Audio unavailable | banner, Listening disabled | "No output device. Collection and the map keep working; nothing will sound." | `state-audio-unavailable.png` |
| Audio device lost | banner | "Output stopped and every note was released." | `state-audio-device-lost.png` |
| Render progress / cancel | Studio strip | progress bar + Cancel | `studio-render-progress.png` |
| Render failed | Studio strip | "Render failed." + host message (alert) | `state-render-failed.png` |
| Unsupported capability | per control | disabled with a reason ("This host does not…", "not offered") | `state-unsupported-capabilities*.png` |
| WebGL unavailable / context lost | map | automatic 2D map, "Collection and sound are unaffected", "Try globe again" | `atlas-2d-map-fallback.png` (manual 2D) |

**Data honesty rules enforced in copy:**

- A microphone detection says "Species only — individual unknown". Its location is the "station position (not the bird's position)".
- An unknown confidence or position is shown as unknown.
- Satellite fixes are joined only as measured points. No interpolation is drawn.
- Every demo surface is labelled DEMO / Demo record / Illustrative demo data. The meter shows **SIM**, and synth-voice counts say "(simulated)".

## 7. Keyboard and accessibility model

**Navigation**

- A skip link goes to the main content. Headers and screens are `header` and `main` landmarks. Side columns are labelled `aside` elements.
- Tab order follows the visual order: header → left column → map controls → globe → loupe stations → Region Mix → tabs → Grain lanes.
- The material mode and other single-choice sets are `radiogroup`s with roving tabindex and arrow keys.

**Globe and map**

- The globe is focusable (`role="application"`): arrow keys rotate (Shift for fine); + / − / reset are buttons.
- Selection by keyboard happens through the loupe/2D-map station buttons, the *Incoming* list or Grain lane buttons (which highlight a species).

**Knobs**

- ←/↓ and →/↑ move ±1 %, with Shift ±0.1 %.
- PageUp/PageDown move ±10 %; Home/End jump to min/max.
- Enter (or clicking the value) opens numeric entry, which accepts display units such as `120 ms`, `-6 dB` or `L12`.
- Delete resets to the inherited/default value. Double-click also resets.
- `aria-valuetext` carries the formatted value.

**Feedback**

- Focus is a 2 px `--c-focus` outline everywhere. SVG stations show a thicker focus-coloured stroke instead.
- Status never relies on colour alone: glyphs (● ↻ ✕ ○), text labels, line height in Grain (full vs short), strike-through for muted species, and dashed vs solid symbols.
- Errors use `role="alert"`. Progress and status use `role="status"` or `aria-live`.

## 8. Compact VST3 editor (proposal only, not built)

Keep the same host adapter. Show a single column:

- header transport and material
- Region Mix
- selected event
- a 2D NZ map (no WebGL)

Leave the library and editing to the standalone app. Collection management stays in the standalone app (single shared collector).
