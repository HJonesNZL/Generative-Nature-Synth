# Control inventory

**Legend**

- **C** = contract method (`contracts.ts`). **P** = provisional method (`provisional.ts`, see INTEGRATION_GAPS.md).
- "Disabled when" always includes "the host does not list the method in `supportedMethods`". That case is not repeated in every row.
- All DSP ranges are **proposals for engineering review**.

**Voice definitions vs synth voices**

- A *voice definition* is the saved recipe edited in Sound Studio (`VoiceDefinition`, with `savedRevision` and `dirty`).
- A *synth voice* is a currently sounding instance in the audio engine. Only its count is shown (`AudioState.activeSynthVoices / maxSynthVoices`). Synth voices are never edited directly.

## Header (both screens)

| Control | Label / role | Purpose | Range / default | Interaction | Reset | Disabled when | Host |
|---|---|---|---|---|---|---|---|
| Screen nav | "Atlas", "Sound Studio" (buttons, `aria-current`) | switch screen without losing state | — | click / Enter | — | never | none (UI) |
| Material | Live · Replay · History (radiogroup) | choose material context | default Live | click, arrows | — | History if `queryHistory` unsupported | none (UI); changes which host queries run |
| Return to start | icon button | seek to 0 | — | click | — | Live; nothing prepared | C `setTransport{seek,"0"}` |
| Play / Pause | icon button | start/pause prepared playback | — | click | — | Live; nothing prepared | C `setTransport{play|pause}` |
| Stop | icon button | stop and rewind | — | click | — | Live; nothing prepared | C `setTransport{stop}` |
| All off | "All off" | panic: release all notes and tails | — | click | — | `setTransport` unsupported | C `setTransport{panic}` |
| Collect | chip → popover | start (1/2/4 h, play while collecting) or stop collection (confirm) | default 4 h, listening on | click; Stop needs confirm | — | `setCollection` unsupported (popover explains) | P `setCollection`, `getCollectionState` |
| Listening | switch | audio enable | — | click / Space | — | device unavailable/lost | P `setAudioEnabled`, `getAudioState` |
| Master | slider "Master level" | output trim | −60…+6 dB, step 0.5, default −6 | drag (begin/change/end), arrows | — (proposal: double-click to 0 dB not implemented) | unsupported | P `setMasterGain` |
| Meter | image with label | peak output | segments every 6 dB from −48 | display only; "SIM" when simulated | — | shows "unavailable" when null | P `getAudioState.peakDbfs` |
| States (DEMO) | icon popover | mock scenarios, simulate automation, reduce motion | — | click | — | not rendered with a native host | mock only |

## Atlas — left column

| Control | Label | Purpose | Range / default | Interaction | Disabled when | Host |
|---|---|---|---|---|---|---|
| Species row | species name + count (toggle) | highlight species on map/loupe/Grain | default Tūī | click toggles | — | P `getCatalog` |
| Source setup… | button | open source setup drawer | — | — | **always (not designed / no host method)** | gap G10 |
| Saved piece row | piece name + badges | open a piece | — | click → prepare | — | C `listSessions`, `prepareReplay` |
| Replay speed | 1× 10× 60× 240× | playback rate (rational n/1) | 60× | radiogroup | no piece | C `prepareReplay` |
| Timeline basis | Observed · Arrival | order by observation or arrival | Observed (replay), Arrival (live) | radiogroup | no piece | C `prepareReplay.basis` |
| History species | chips (multi) | species to query | Tūī, Korimako | toggle | — | C `queryHistory.speciesIds` |
| Region | select | query region | Central NI forest | select | — | C `listRegions`, `queryHistory.regionId` |
| Date / From / To | date + time inputs | requested window | today 05:00–08:00 | type | invalid shows inline message | C `queryHistory.fromUtc/toUtc` |
| Timezone | select | interpretation of date/time | Pacific/Auckland; UTC | select | — | C `queryHistory.displayTimezone` |
| Check available data | button | run query | — | click | invalid draft; busy | C `queryHistory` |
| Name | text input | name of frozen piece | "⟨species⟩ · ⟨date⟩" | type | — | P `freezeHistorySelection.name` |
| Playback length | radio | keep requested interval vs trim to records | keep | radio | — | P `preserveRequestedInterval` |
| Freeze soundscape | primary button | freeze selection, then prepare | — | click | no name; status unavailable; zero records | P `freezeHistorySelection` → C `prepareReplay` |

## Atlas — map

| Control | Label | Purpose | Interaction | Disabled when | Host |
|---|---|---|---|---|---|
| Map view | Globe · 2D map | renderer choice | radiogroup | Globe if WebGL unavailable | none |
| Zoom in/out, Reset view | icon buttons | camera | click | 2D view | none |
| Reduce motion | pill (pressed) | stop pulses/damping/transitions | click | — | none (UI; follows OS by default) |
| Globe | focusable application | rotate / pick | drag; arrows (Shift fine); click marker | — | none |
| Station / fix (loupe, 2D) | button per marker, labelled with counts | select latest event there | click / Enter / Space | — | data from events |

## Atlas — right column (H2 Region Mix + inspector)

| Control | Label | Purpose | Range | Interaction | Disabled when | Host |
|---|---|---|---|---|---|---|
| Change region | link-styled select | region for the mix | listRegions | select | — | C `listRegions`; P `getRegionActivity` |
| S (solo) | "Solo ⟨species⟩" pressed toggle | sound only soloed species | — | click; waits for ack | no voice; unsupported | P `setSpeciesMix{soloed}` |
| M (mute) | "Mute ⟨species⟩" | stop sounding (records kept) | — | click; waits for ack | no voice; unsupported | P `setSpeciesMix{muted}` |
| Assign | dashed pill | go to Studio for a species without voice | — | click | — | navigation; assignment is gap G9 |
| Tabs | Selected · Incoming/Events | inspector vs list | — | click, ←/→ | — | — |
| Incoming row | time · species · place · outcome | select event | latest 40 | click / Enter | — | event store |
| Open voice in Sound Studio | primary button | jump to matching voice | — | click | no species | P `getVoiceLibrary` / outcome voiceId |

## Grain (bottom, both screens)

| Control | Purpose | Interaction | Notes |
|---|---|---|---|
| Lane name button | highlight species | click / Enter (pressed state) | up to 6 lanes |
| Lane strip | pick nearest event | mouse click only | keyboard path: Incoming list |
| Coverage band | read-only | — | list of segments with text for screen readers |

## Sound Studio

| Control | Label | Purpose | Unit / range / default (proposal) | Interaction | Reset | Disabled when | Host |
|---|---|---|---|---|---|---|---|
| Library row | species + source text | select the voice that sounds for it | — | click | — | — | P `getVoiceLibrary` |
| Group default | pill | select group definition | — | click | — | group has none | P |
| + Assign animal voice | button | create/assign voice | — | — | — | **always (gap G9)** | — |
| Undo / Redo | icon buttons | step through unsaved gestures | 50 steps | click | — | empty stack | C `setVoiceParameter` (re-sent) |
| Revert | quiet button | discard unsaved changes | — | click | — | not dirty | P `revertVoice` |
| Save voice | primary | persist definition | expectedRevision | click | — | not dirty | P `saveVoice` |
| Voice type | Pluck · Air · Resonance · Bell | synthesis family | — | radiogroup | — | unsupported | P `setVoiceType` |
| Tuning | knob | transpose | −24…+24 st, linear, default 0 | drag/keys/type | Delete → inherited | edit unsupported | C `setVoiceParameter` |
| Attack | knob | envelope attack | 0.001–2 s, log, 0.02 | " | " | " | C |
| Decay | knob | envelope decay | 0.01–4 s, log, 0.4 | " | " | " | C |
| Sustain | knob | envelope level | 0–1, linear, 0.7 | " | " | " | C |
| Release | knob | release / tail | 0.01–8 s, log, 1.2 | " | " | " | C |
| Tone | knob | brightness macro | 0–1, linear, 0.68 | " | " | " | C |
| Gain | knob | voice level | −48…+6 dB, linear, −6 | " | " | " | C |
| Pan | knob | base pan before mappings | L50…R50, linear, C | " | " | " | C |
| Mapping toggles | switch per row | enable an event→music mapping | — | click | — | unsupported | P `setMappingEnabled`, `getMappings` |
| Audition selected event | outline | host plays the event through this voice | — | click | no event/voice; unsupported | P `auditionEvent` |
| Render WAV… | outline | offline render of open/frozen piece | WAV stereo 48 kHz 24-bit, tails ring | click | no piece; rendering | P `startRender` |
| Cancel (render) | quiet | cancel render | — | click | — | — | P `cancelRender` |
| Draw region | link | create region geometry | — | — | — | **always (gap G11)** | — |
| Region name | button | select region | — | click | — | — | C `listRegions` |
| Effect on/off | switch | enable/bypass region effect | — | click; waits for ack | — | unsupported | P `setRegionEffect{enabled}` |
| Send | slider | region send level | 0–1, step 0.01 | drag/keys (begin/change/end) | — | effect bypassed | P `setRegionEffect{send,gesture}` |
| Overlap | select | region overlap resolution | Highest priority wins · Blend sends | select | — | unsupported | P `setRegionPolicy` → `RegionView.overlapPolicy` |
| Uncertain position | select | how uncertain positions route | Use reported point · Skip region effects | select | — | unsupported | P `setRegionPolicy` |

## Behaviours shown but not decided (need engineering decisions)

| Topic | Current presentation | Open question |
|---|---|---|
| Inheritance (group → species → individual) | Rows show *species voice / inherits group / override / inherits*. Knobs mark overrides with •. Delete resets to the parent value. | Does saving a parent propagate to children immediately? How are overrides cleared? |
| Region overlap | single policy for all regions (`overlapPolicy` on each RegionView) | Per-region or global? How does blend weight sends? |
| Dry/wet | direct sound always kept; region adds a send ("send" 0–1) | Per-voice dry level? Wet/dry on the effect itself? |
| Mute/solo | species-level, global to the piece | Per-region solo? Saved in the session? |
| Panic | releases notes and tails immediately | Should tails fade over N ms instead of hard-cut? |
| Tails on region change | not shown | Which bus owns a tail when an event changes region? |
| Undo scope | per definition, unsaved gestures, this view | Should the host keep a durable undo history? |
