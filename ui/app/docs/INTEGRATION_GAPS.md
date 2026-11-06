# Integration gaps and proposed API additions

**Contracts**

- `src/host/contracts.ts` is the supplied v1 contract, copied unchanged.
- `src/host/provisional.ts` defines the extra methods and notifications the UI uses. The mock implements all of them. **None of them exists natively.**
- The UI checks `getCapabilities().supportedMethods` before every call. If a method is missing, its control is disabled with a reason; the UI does not fail.

**Envelope (unchanged from contract v1)**

- Request: `{ protocolVersion: 1, requestId, method, args }`
- Reply: `{ requestId, ok: true, result }` or `{ requestId, ok: false, error: { code, message } }`.

`code` is one of `unsupported | invalid_request | unavailable | stale_generation | internal`. The client adds the local codes `timeout` and `aborted`.

## Changes requested to the v1 contract itself

| # | Change | Why |
|---|---|---|
| C1 | `HostCapabilities.supportedMethods: string[]` (currently `Array<keyof HostMethods>`) | The host must be able to advertise provisional/extension methods. The mock has to cast today. |
| C2 | Add a push channel. `AtlasHost.subscribe(listener)`, or a `HostNotification` message type on the same bridge. | Transport position, automation, render progress, source health and new-arrival wake-ups. Without it the UI polls, which is acceptable for display but wasteful. See G1. |
| C3 | `EventView` has no station or source **names**, so a lookup is needed (G2). Alternatively, add `stationName` to `EventView`. | Inspector and labels. |
| C4 | `getChanges` has no musical outcome (sounded/grouped/filtered/why). | Brief §5 requires it. Proposed as G3 rather than widening `EventView`. |
| C5 | `queryHistory` returns no way to fetch the selected events. | Map and Grain need them (G6). |
| C6 | `setTransport` has no "current state" query. | After reconnect the UI must re-read transport. Propose `getTransport() → { generation, state, positionUs, durationUs, preparationId }`. |
| C7 | `prepareReplay.sessionId` must also accept a frozen history id. | The mock accepts `frozen-…` ids. |

## Provisional methods (proposed additions)

Each entry gives the request, a result and an error. JSON is shown as it crosses the bridge. All 64-bit and microsecond values are strings.

### G1 · Notifications (`subscribe`)

```json
{ "type": "transportChanged", "generation": "7", "state": "playing", "positionUs": "81250000" }
{ "type": "voiceParameterChanged", "voiceId": "voice-tui", "parameterId": "tone", "normalizedValue": 0.51, "changeRevision": "211", "origin": "automation" }
{ "type": "renderProgress", "job": { "jobId": "render-1", "state": "rendering", "progress": 0.4, "message": null } }
{ "type": "changesAvailable", "storeId": "lib-3f2a", "latestSequence": "1793961200000004" }
{ "type": "connection", "state": "reconnecting" }
```

Also `sourceHealthChanged`, `audioStateChanged` and `collectionChanged`, which carry no payload: the UI re-queries. Notifications must be idempotent and must never carry provider text intended for script evaluation.

### G2 · `getCatalog`

- Request: `{}`
- Result: `{ "species": [{ "id": "tui", "commonName": "Tūī", "scientificName": "Prosthemadera novaeseelandiae", "group": "forest_bird" }], "stations": [{ "id": "stn-rotorua", "name": "Rotorua", "sourceId": "birdweather", "latitude": -38.14, "longitude": 176.25, "positionDescription": "Station position" }], "sources": [{ "id": "birdweather", "name": "BirdWeather", "measurement": "Acoustic species detection at fixed stations", "attribution": "…" }] }`
- Error: `{ "code": "unavailable", "message": "Library not open" }`

### G3 · `getMusicalOutcomes`

- Request: `{ "eventIds": ["evt-1", "evt-2"] }` (at most 500 ids)
- Result: `{ "evt-1": { "kind": "sounded", "voiceId": "voice-tui", "noteName": "D4", "velocity": 72, "pan": -0.36, "regionSends": [{ "regionId": "region-central-forest", "effectId": "fx-forest-space", "send": 0.6 }] }, "evt-2": { "kind": "filtered", "reason": "species_muted" } }`
- Kinds: `sounded`, `grouped{groupId, groupSize}`, `filtered{reason}`, `suppressed{reason: density_cap|duplicate|baseline|correction}`, `deferred{scheduledForUtc}`, `not_evaluated`.
- Error: `{ "code": "invalid_request", "message": "at most 500 ids per request" }`

### G4 · `getRegionActivity`

- Request: `{ "regionId": "region-central-forest", "windowMinutes": 60, "basis": "arrival", "material": { "kind": "session", "sessionId": "…" } }`. Omit `material` for live.
- Result: `{ "regionId": "…", "windowFromUtc": "…", "windowToUtc": "…", "basis": "arrival", "species": [{ "speciesId": "tui", "detections": 12, "share": 0.41, "voiceId": "voice-tui", "voiceLabel": "A · Pluck", "muted": false, "soloed": false }], "totals": { "records": 29, "eligible": 26, "sounded": 19, "grouped": 1, "suppressed": 3 } }`
- The host owns region membership (station/fix positions, uncertainty policy). The UI never does point-in-polygon tests.
- Open: should `share` count detections (current) or sounded gestures?

### G5 · Mix, audio and collection

| Method | Request | Result | Example error |
|---|---|---|---|
| `setSpeciesMix` | `{ "speciesId": "kaka", "muted": true }` | `{ "speciesId": "kaka", "muted": true, "soloed": false, "changeRevision": "212" }` | `invalid_request: unknown species` |
| `getAudioState` | `{}` | `{ "status": "running", "deviceName": "…", "masterGainDb": -6, "peakDbfs": -14, "meterIsSimulated": false, "clipping": false, "activeSynthVoices": 3, "maxSynthVoices": 32 }` | — |
| `setAudioEnabled` | `{ "enabled": false }` | AudioState | `unavailable: no output device` |
| `setMasterGain` | `{ "gainDb": -4.5, "gesture": "change" }` | `{ "gainDb": -4.5, "changeRevision": "213" }` | `invalid_request: gain must be finite` |
| `getCollectionState` | `{}` | `{ "state": "collecting", "listening": true, "requestedDurationMinutes": 240, "startedAtUtc": "…", "elapsedUs": "4360000000", "recordCount": 143, "storageWarning": "none" }` | — |
| `setCollection` | `{ "action": "start", "durationMinutes": 120, "listening": false }` | CollectionState | `invalid_request: already collecting` |

### G6 · History material

| Method | Request | Result | Example error |
|---|---|---|---|
| `freezeHistorySelection` | `{ "selectionId": "sel-9a", "name": "Tūī · 07 Nov 2026", "preserveRequestedInterval": true }` | SessionView + `requestedFromUtc/ToUtc`, `materialFromUtc/ToUtc` | `invalid_request: unknown or expired selection` |
| `getMaterialEvents` | `{ "material": { "kind": "selection", "selectionId": "sel-9a" }, "offset": 0, "limit": 500 }` | `{ "total": 136, "events": [EventView…] }` | `unavailable: archive offline` |

### G7 · Voices

| Method | Request | Result | Example error |
|---|---|---|---|
| `getVoiceLibrary` | `{}` | `{ "entries": [VoiceLibraryEntry…], "parameters": [VoiceParameterSpec…] }` | — |
| `getVoice` | `{ "voiceId": "voice-tui" }` | VoiceDefinition | `invalid_request: unknown voice` |
| `setVoiceType` | `{ "voiceId": "voice-tui", "type": "air" }` | VoiceDefinition (dirty) | — |
| `saveVoice` | `{ "voiceId": "voice-tui", "expectedRevision": "12" }` | VoiceDefinition (rev 13, clean) | `stale_generation: voice was saved elsewhere` |
| `revertVoice` | `{ "voiceId": "voice-tui" }` | saved VoiceDefinition | — |
| `getMappings` | `{ "voiceId": "voice-tui" }` | `[MappingRow…]` | — |
| `setMappingEnabled` | `{ "voiceId": "voice-tui", "mappingId": "map-longitude", "enabled": false }` | MappingRow | — |
| `auditionEvent` | `{ "eventId": "evt-1", "voiceId": "voice-tui" }` | `{ "accepted": true }` | `unavailable: audio off` |

`setVoiceParameter` (contract) is reused for knobs. The host must:

- echo `changeRevision`
- group each begin…end gesture into one undo/automation step
- emit `voiceParameterChanged` with `origin`

### G8 · Region effects and render

| Method | Request | Result | Example error |
|---|---|---|---|
| `listEffects` | `{}` | `[{ "id": "fx-forest-space", "name": "Forest Space", "kind": "reverb", "summary": "reverb · 3.8 s" }]` | — |
| `setRegionEffect` | `{ "regionId": "…", "effectId": "…", "send": 0.55, "gesture": "change" }` | RegionView | `invalid_request: effect not assigned to region` |
| `setRegionPolicy` | `{ "overlapPolicy": "blend", "uncertainPosition": "skip_region_effects" }` | `{ "overlapPolicy": "blend", "uncertainPosition": "skip_region_effects" }` | — |
| `startRender` | `{ "sessionId": "…", "format": "wav_stereo_48k_24", "tailPolicy": "let_ring" }` | RenderJob | `internal: piece cannot be read` |
| `getRenderJob` / `cancelRender` | `{ "jobId": "render-1" }` | RenderJob (`queued | rendering | complete | failed | cancelled`) | `invalid_request: unknown render job` |

**Render file location:** the host shows the native save dialog *before* rendering. The UI never sends paths.

## Not designed / not offered (UI shows disabled)

| # | Missing capability | UI today |
|---|---|---|
| G9 | Create or assign a species voice; create an individual override; delete an override | "+ Assign animal voice" and the "Assign" pills navigate only |
| G10 | Source setup (enable/disable sources, permissions, credentials) | "Source setup…" disabled |
| G11 | Region geometry editing (draw, rename, priority, delete; antimeridian handling) | "+ Draw region" disabled |
| G12 | Session save/open/export (bundle files), dirty state for region/mix edits, presets (save/load voice presets separately from sessions) | Not shown. Needs native dialogs and a session dirty model. |
| G13 | Audio device selection and buffer settings | Banner only. Belongs in a host preferences dialog. |
| G14 | Bookmarks, loop range, seek by clicking the timeline | Not shown (no `setLoop`/bookmark methods; seek only to 0) |
| G15 | Import (CSV/JSON/GeoJSON/GPX) with validation results | Not shown |

## Security notes for the bridge

- No raw script evaluation built from provider text. Every provider string is rendered as text by React.
- No bearer tokens, credentials or filesystem paths in page URLs, JS bundles or bridge payloads.
- Suggested CSP for the served `index.html`:

  ```
  default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; font-src 'self'; connect-src 'self'
  ```

  Here `self` is the resource-provider origin. `unsafe-inline` styles cover React style attributes and could be tightened with hashes.
