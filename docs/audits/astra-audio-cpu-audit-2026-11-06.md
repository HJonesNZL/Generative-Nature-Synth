# Animal Atlas: audio, realtime and CPU audit

6 November 2026. Independent GPT-6 Astra review. Read-only implementation audit; the only project change made by this reviewer is this report. No collector, watcher, database, session, audio device or DAW state was changed. Temporary compilation and synthetic tests used `/private/tmp/atlas-astra-audio-q0r7spug`.

## Conclusion and evidence boundary

The existing native event scheduler is a sound starting point for an instrument: construction is separated from bounded immutable block lookup; timing uses checked integer arithmetic; data records preserve observation versus arrival meaning; overload is reported. There is no implemented synth, DSP effect, voice manager, realtime queue, JUCE processor or React host to certify or diagnose. The documentation says this accurately. Priorities below distinguish one demonstrated implementation inefficiency from contracts that must be settled before audio integration.

The most consequential next component is a **musical-action compiler**, followed by a small voice engine. A data event is not yet a note. Animal identity, density decisions, note lifetime, geographic routing and control changes must become reproducible bounded actions before the realtime callback sees them. Otherwise a musically convincing mockup can hide nondeterministic rendering, stuck voices and resource growth.

Reviewed the original owner handoff, current architecture/protocol/session documents, Direction H feature brief, C++ headers/sources/tests, Python scheduling/acquisition/session code and TypeScript contracts. Applied the Field audio DSP skill and its effect-design, realtime and time/space capsules. This is evaluation and specification work, not approval of a final sound palette. Desired musical qualities are intelligible animal identity, articulate attacks and controlled continuity during sparse activity; no listening evidence exists yet.

## Findings in priority order

### P1 integration gate: freeze musical admission independently of host blocks

[The native block method](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/native/src/core.cpp:175>) retains the first `output.size()` records in each block. This is an honest data API, but using its omissions as the musical density policy makes the piece depend on the host's block partition. The limitation is already disclosed in [architecture](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/docs/native-react-architecture.md:90>) and [replay documentation](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/docs/sessions-and-replay.md:47>).

Reproduced with four synthetic events at 0, 500, 1000 and 1500 microseconds, 48 kHz, capacity two. With 32-frame blocks all four survive. With 64-frame blocks event three is omitted. This is not a new regression; it is the concrete reason this API cannot be the final admission layer.

Compile selection, repeated-call grouping, quotas, deterministic variation, timing transforms, note durations and routing on a worker. Freeze the accepted actions and reasons for every suppressed/grouped input. Define whether grouping windows and quotas operate in observation time, arrival time or accelerated musical time; these are materially different compositions. For live arrival batches, explicitly choose immediate burst, grouping or bounded redistribution and preserve both arrival and assigned musical time. Initial BirdWeather polling is at least 60 seconds and can fetch up to 1,000 records per poll ([collector](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/animal_atlas/collector.py:9>), [source pagination](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/animal_atlas/sources.py:108>)); smooth provider timestamps do not imply smooth live delivery.

Use a versioned numeric action schema: generation, stable action/event identity, due render frame or well-defined logical time, voice-definition handle, note-instance identity, pitch, velocity, bounded duration, routing and parameter snapshot references. Keep original record links off the callback. Compute region classification, string/identity lookup and provider-specific confidence rules off-thread. Rendering can then slice already-admitted actions without changing which notes exist. Guarantee bounded action density for the supported internal processing quantum, including simultaneous events; define and count any final defensive overflow separately. Musical filtering is not permission to drop note releases or panic.

### P1 integration gate: voice and immutable-state lifetimes need an executable contract

[`BlockTrigger`](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/native/include/atlas/core.hpp:44>) only identifies an event and sample offset. No note-release or voice-lifetime mechanism exists. [The proposed transport API](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/ui/contracts.ts:96>) returns a generation but does not require the caller's expected generation, identify note instances or specify stale-command ordering. These are omissions in a proposed interface, not current audible failures.

Separate an animal's saved **voice definition** from a sounding **note instance**. Hundreds of species definitions should share a fixed active-note pool, not instantiate hundreds of running synths and effects. Define per-definition and global polyphony, admission priorities, deterministic steal ordering, a bounded steal fade, maximum lifetime and release completion. An accepted note can carry its release frame in its voice state, avoiding dependence on a future release message surviving a crowded note-on queue.

Specify state transitions for pause, stop, seek, loop, source mute, solo, collector disconnect, session replacement and global panic. Disconnect should not leave a sustained voice dependent on a network note-off. Panic must work without waiting behind pending events. A generation change must invalidate prepared work and pending note starts; resetting audio state alone is insufficient if stale worker messages can arrive afterwards.

Publish immutable plans/configurations through fixed slots or another explicitly proven handoff. Retire and destroy the old objects on a worker after acknowledgement. An atomic pointer alone does not establish lifetime, and reference counting must not cause the callback to destroy the last large session/graph owner. If using JUCE `AbstractFifo`, honour its single-reader/single-writer topology, reserve its actual usable capacity and never spin until room appears in audio code. [JUCE FIFO contract](https://docs.juce.com/master/classjuce_1_1AbstractFifo.html).

### P1 integration gate: define one authoritative musical clock and host state machine

[Native timeline construction](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/native/src/core.cpp:127>) fixes one speed at preparation; [block lookup](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/native/src/core.cpp:165>) is stateless. Neither implements running speed changes, host grid quantization, transport synchronization or state restoration. [Python replay](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/animal_atlas/replay.py:59>) explicitly delegates these policies.

Define mappings among UTC event time, monotonic live receipt time, musical session position, host PPQ and integer render sample position. Audio timing must use the rendering clock, not wall time or webview animation. Re-anchor speed changes at the current logical position; rebuilding at a new speed from time zero must not jump the piece. State how durations behave when event spacing is accelerated, and which time domain tempo-linked effects follow. Quantization requires a bounded lookahead/delay policy or a declared next-grid action; it cannot retroactively place a newly received event in the past.

Default to a small documented transport set: saved-session host-following and explicit standalone/free-running live mode. Decide stopped-host admission, missing tempo/playhead data, negative host positions/preroll, loop wrap, host seek, device suspension and sample-rate changes. The native block interface rejects zero frames and more than 65,536 frames; a JUCE adapter must handle zero-length callbacks and safely subdivide larger buffers without allocation or changing action admission. JUCE documents variable/zero buffers and optional timing information. [AudioProcessor](https://docs.juce.com/master/classjuce_1_1AudioProcessor.html), [PositionInfo](https://docs.juce.com/master/classjuce_1_1AudioPlayHead_1_1PositionInfo.html).

Freeze events, admitted actions, mappings, parameter trajectories, routing, regions, seed algorithm and engine version for offline rendering. Current [session options](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/animal_atlas/sessions.py:60>) preserve data-session settings, not a musical performance. Distinguish quick seek that resets tails from faithful rendering with bounded preroll/checkpoints. Offline render must work with collector and webview absent and must not wait for real time.

### P1 integration gate: regional FX must have finite topology and tail ownership

[RegionView](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/ui/contracts.ts:70>) proposes arbitrary polygon arrays and effect assignments, while [the feature brief](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/deliverables/Animal_Atlas_Direction_H_Design_Handoff/ANIMAL_ATLAS_FULL_FEATURE_BRIEF.md:175>) correctly leaves overlap, transition and tail decisions open. No finite bus count, chain depth, crossfade capacity, topology or bypass contract exists yet.

Start with animal voices feeding controllable direct sound plus a small fixed set of shared regional wet buses; one delay/reverb instance per active regional bus, not per animal event. Compile polygon membership on a worker. Define deterministic equal-priority ties, antimeridian handling, unknown positions and uncertainty crossing a border. A highest-priority policy is the least complicated first slice. If blending is later added, cap the number of sends and define gain normalization; coherent signals do not obey the same gain rule as unrelated signals.

At note onset, latch routing for that gesture unless an explicit continuous-motion mode is authored. A later tracking fix or polygon edit should not silently teleport an existing tail. Tail state belongs to its effect bus. Stop new sends and let it decay, fade it, or clear it according to a documented operation. Once several animals share a wet bus, removing only one animal's already-mixed tail is impossible without retained per-source state; source mute semantics must reflect that. Global panic can clear all buses.

Limit simultaneous retiring buses/graphs and account for old/new processing during transitions. Repeated edits need a bounded latest-pending replacement policy, not an ever-growing list of fading graphs. Initial routing should prohibit inter-region feedback. For each feedback effect, require a decay/energy argument valid during modulation, not merely a feedback knob below one. Delay interpolation, changing-time pitch movement, damping and decay are part of the sound specification. The relevant theory is documented in Julius Smith's [FDN treatment](https://ccrma.stanford.edu/~jos/pasp/FDN_Reverberation.html); do not copy its published implementation constants or layouts.

### P2 demonstrated CPU issue: sorting copies long IDs for every comparison

[The comparator](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/native/src/core.cpp:134>) constructs value tuples containing `a.id` and `b.id`. Those strings are copied even when timestamps settle the comparison. This turns normal long provider IDs into heap work throughout `O(N log N)` sorting. It is worker-side inefficiency, not an allocation in `Timeline::block`.

Temporary Apple Clang 17, arm64, `-std=c++20 -O2` test, 50,000 deterministic synthetic events and IDs longer than small-string storage:

| Constructor under test | Counted C++ allocations | Three elapsed times |
|---|---:|---|
| Unchanged project source | 1,745,605 | 64.3, 53.1, 53.5 ms |
| Temporary copy with string-reference tuple comparison | 1 | 12.2, 8.7, 7.5 ms |

The narrow correction is lexicographic comparison by time, revision and string reference. The temporary variant kept those ordering keys exactly. This was a synthetic local measurement with allocation instrumentation, not a representative DAW benchmark or a hardware-qualified CPU claim. No source fix was made by this reviewer. The coordinating agent has elected to fix only this measured comparator issue; its remediation and verification are pending at report handoff. Add ordering parity and preparation-allocation checks when fixing it; do not optimize the bounded callback search before profiling it.

Also avoid repeated plan construction for UI pagination: [the Python timeline endpoint](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/animal_atlas/api.py:157>) rebuilds and verifies the whole plan per page, and [Timeline.page](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/animal_atlas/replay.py:68>) recalculates the one-second peak. This is already acknowledged prototype behavior. The native successor should cache bounded immutable preparations by content/settings fingerprint and reuse summaries, with explicit cancellation/eviction. Do not modify the active collection workflow merely for this optimization.

### P2 design gate: parameter, numerical and sound contracts precede a large DSP suite

[`setVoiceParameter`](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/ui/contracts.ts:100>) has arbitrary IDs and a normalized number but no parameter catalogue, physical units, defaults, rate, smoothing, versioning or notification subscription. It also does not distinguish a saved animal voice definition from a currently sounding voice. Define those semantics before UI controls become the de facto audio API. Native validation must reject non-finite and out-of-range values. Stable host-automatable slots/macros are a safer first contract than an unbounded host-parameter list that changes when species are discovered.

Every admitted primitive needs independently derived equations and fixtures, finite input/control/state policy, parameter trajectories, update order, rate/layout changes, tail/reset/bypass and latency accounting. Smooth in meaningful coordinates where appropriate: gain in dB, frequency/time logarithmically, feedback in a safe bounded parameterization. Check equal/zero time deltas and missing metadata before movement/rate mappings. Do not enable blanket fast-math assumptions that invalidate the specified non-finite policy.

Use a platform-level denormal policy with portable tests of silent/near-silent tails; JUCE provides a wrapper helper, not a stability proof. Read atomic parameter targets as appropriate; do not copy/replace an entire ValueTree in processing. JUCE explicitly documents state-copy/replace locking. [ScopedNoDenormals](https://docs.juce.com/master/classjuce_1_1ScopedNoDenormals.html), [AudioProcessorValueTreeState](https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState.html).

Begin with one simple original envelope/oscillator voice, fixed pitch/range/headroom and no nonlinear oversampling, then audition a contrasting voice. Add modest shared wet effects only after timing and lifetime tests pass. Defer spectral resynthesis, convolution, per-animal reverbs, unrestricted feedback graphs and dynamic oversampling until their audible benefit justifies their memory, latency and CPU costs. This is an implementation-risk recommendation, not a claim that one sound design has already won a listening test.

### P2 qualification gate: explicit audio and graphics resource envelopes are missing

[The architecture](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/docs/native-react-architecture.md:80>) sensibly proposes batching, instancing, throttling and a 2D fallback. These are requirements, not measured results. Define limits before exposing arbitrary regions, voices and effects. A reasonable initial experiment is 16 active notes, at most two shared wet buses and one bounded transition, then measure before increasing. This is a provisional experiment capacity, not an approved product limit or a demonstrated performance result.

Budget active and releasing notes, musical actions per time window, coefficient work, maximum delays at maximum sample rate, wet buses including retiring tails, plan slots, visible markers, labels/pulses, texture bytes and bridge batch sizes. Large session JSON should not be duplicated into realtime state; compact numeric actions suffice. Inactive definitions cost no per-sample processing. Skip a wet bus only after a conservative state/tail-silence decision, not because it received no input this block.

Measure callback p50/p95/p99/max and deadline overruns in a release build on named hardware/OS/host at declared rates and buffers. A 64-frame/48 kHz period is 1.333 ms; 32 frames/96 kHz is 0.333 ms. These are the total callback periods, not a plugin's exclusive budget. Agree a headroom target, then test peak voices, simultaneous events, longest tails, rapid automation, graph transitions and other active instances. Average process CPU alone cannot qualify this.

Profile audio with Atlas visible and animating, Sound Studio visible, editor hidden/closed, large history selection, WebGL failure and multiple plugin instances. Coalesce UI control drags and meter updates; a webview restart must neither reload the synth nor retrigger events. Inactive/hidden globe animation should stop or strongly throttle, and headless rendering should not create it. Three.js resources require explicit disposal; inspect graphics resource counts across repeated editor opening/closing and view switches. [Three.js disposal documentation](https://threejs.org/manual/pages/how-to-dispose-of-objects.html).

## Narrow delivery sequence and gates

1. **Contract and action compiler.** Freeze a musical schema and versioned density/mapping policy using synthetic edge cases and saved permitted local material. Gate: identical action IDs, order and timing across chunking and render partitions; rare-event quotas, repeated-call groups and every suppression reason reproducible.
2. **Headless reference voice.** Implement one portable scalar voice and fixed voice pool against an offline oracle; then a thin native audio adapter. Gate: start/release/steal/panic, silence and extreme parameters are finite, bounded and click-controlled; waveform tolerances stated; no callback allocate/free/lock/I/O. A full production collector rewrite and elaborate globe are not prerequisites to auditioning frozen material.
3. **Transport and lifecycle.** Add sample-clock/generation ownership, live worker bridge and frozen-session offline render. Gate: seek/loop/stop/mute/disconnect and rapid plan changes leave no stale notes; zero/variable/oversized buffers, missing host position and rate changes are safe; late batches and speed changes obey documented policies.
4. **Two regional spaces.** Add worker-side region classification and a small fixed wet topology. Gate: impulse routing, direct/wet timing, mono/stereo behavior, decay, hostile automation, longest tails and repeated edits pass; total processing includes transitions and retired tails.
5. **JUCE plus minimal embedded UI.** Native host state remains authoritative across Atlas/Sound Studio. Gate: editor lifecycle, automation and restore work with collector absent; views cannot duplicate triggers; resources plateau across repeated opens; local-only assets and compatible zero-cost dependencies are confirmed before distribution.
6. **Actual host qualification.** Test standalone and VST3 in Ableton, then other supported targets. Open `.als`/`.amxd` only through Finder right-click → Open With → Ableton Live 12 Suite without `_updated`, per owner instruction. Gate: deadline and overload evidence under full UI/audio stress, offline bounce, state restore and multi-instance operation. No such host test was performed in this audit.

## Proposed project realtime rules

1. The callback must not allocate or free heap memory, lock/spin/wait, perform I/O/JSON/SQLite, log, touch UI or throw.
2. Preallocate and name every voice/action/delay/region/transition capacity; every callback loop has an explicit finite bound.
3. Compile compact numeric musical actions on workers; keep provider parsing, string lookup, polygons and confidence semantics outside audio processing.
4. Make musical admission independent of host partitions; preserve provenance and omission reasons. Note release, cancellation and panic must survive note-on overload.
5. Specify publication and retirement ownership. The callback never destroys the last owner of a plan/graph or resets a queue concurrently with its peer.
6. Use an explicit sample clock and generation. Seek, loop and replacement invalidate stale work; wrappers safely handle zero, variable and oversized blocks.
7. Test voice release/steal, lifetime, stop/mute/disconnect and tail policies before integrating a new voice or processor.
8. Every DSP specification declares ranges, smoothing, update order, finite recovery, denormals, rate changes, latency and stability bounds.
9. Keep geographic wet topology finite; compile routing off-thread. Transitions and retiring tails count against the same CPU/memory envelope.
10. Version stable parameter IDs/units/ranges and the randomness algorithm. Do not use insertion order or implementation-defined hashing for audible identity. Save timed control changes when claiming performance reproduction.
11. Frozen offline rendering cannot require a collector, webview, network or wall-clock waiting. Declare duration/seek/tail and DSP-version behavior; test action determinism separately from waveform equality.
12. Qualify release-build deadlines under peak load on named hardware/host, including visible/hidden UI and multiple instances. Publish counters off-thread and distinguish scheduler checks from DSP/listening/DAW proof.

## Verification performed and limits

- Independently compiled and ran the existing C++ core suite in `/private/tmp`: **16 passed, 0 failed**.
- Added no project tests or implementation. A temporary synthetic harness reproduced overload partition dependence and counted/measured the sort comparator allocation difference.
- The current [allocation test](</Users/henryjones/Documents/ChatGPT/Generative Nature Synth/native/tests/core_tests.cpp:151>) covers ordinary C++ allocations in one prepared block lookup. It does not qualify a complete renderer, lifecycle, all allocation APIs, callback timing or a DAW.
- No live source request, owner database query, collector interruption, DSP listening, rendering, UI runtime or host test was performed by this reviewer. No audible-quality, deadline, leak-free, cross-platform or bit-identical-audio claim is established.
- Primary API/theory documentation was checked for JUCE callback/FIFO/state rules, playhead optionals, denormals, feedback-reverb theory and Three.js cleanup. The implementation recommendations are this review's synthesis; no public DSP source was copied.
