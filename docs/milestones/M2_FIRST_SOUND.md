# M2 first sound — offline engineering checkpoint

7 November 2026, beta-1. **Verified saved piece → deterministic native synthesis → traceable WAV is implemented.** M2 remains open for human listening and a working device transport. This is an experimental first voice, not the final instrument palette or release qualification.

## What can be used

`Audition Atlas Session.command` renders the latest saved piece through the portable C++ engine to a new audition folder, then asks before playing it. It reads the library without modifying it. The launcher was checked with disposable data and playback declined; the owner's library and active collector were not used for this qualification. The CLI also accepts an explicit session, timing basis, playback speed, sample rate and synthesis settings; see [the audio contract](M2_AUDIO_CONTRACT.md).

The engine prepares numeric musical actions away from processing, admits events independently of block size, and renders a bounded three-partial voice with defined attacks, decay, stealing and transport transitions. Stereo 24-bit WAV includes verified session identity, synthesis settings, admission counts and source-build identity. Export refuses existing destinations and publishes only a completed file. It requires a filesystem supporting its hard-link publication strategy; this is not a power-loss durability guarantee.

The UI remains a read-only native data preview, with a separate labelled demo. Offline rendering does not enable in-app audio controls. The [frontend integration readiness report](FRONTEND_INTEGRATION_READINESS.md) identifies the remaining native interfaces. [Globe preview scope](GLOBE_PREVIEW.md) describes bounded loaded-record coverage, not global completeness.

## Evidence and boundaries

All six CTest suites passed in Debug, Release and AddressSanitizer/UndefinedBehaviorSanitizer configurations after the receiver catalogue correction. These include 18 core cases, SHA vectors, 11 audio groups, 18 storage integration cases, 8 native UI cases and 13 independent WAV cases. Project warnings are errors. The audio and WAV checks cover independent equations, partition invariance, dense admission/stealing, allocation/deallocation probes, control transitions, invalid input, sample rates, complete historical windows, metadata and output refusal. See [final Debug](../../evidence/m2-audio/final-native-debug.log), [Release](../../evidence/m2-audio/final-native-release.log) and [sanitizer](../../evidence/m2-audio/final-native-sanitized.log) logs.

[Final build identity](../../evidence/m2-audio/final-build.json) records the exact Release artifact and production-source SHA. [Source hashes](../../evidence/m2-audio/final-source.json) were checked unchanged after qualification. The immutable local artifact is retained under `build/m2-qualified-c02b5d1886fb/`; generated binaries and audio are ignored by git, while their identities and the generator are retained.

[Independent native review](../../evidence/m2-audio/native-review.md) found no confirmed actionable audio defect in the earlier `743cadf5898d` source build. The later native change only excludes individual GPS fixes from receiver catalogues in `ui_host.cpp`. Audio, timeline, WAV and render CLI sources are unchanged. The final suites were rerun, but the earlier independent review, process timings and first listening-material measurements remain explicitly attributed to their original build; their hashes are not relabelled as a new run.

[Process timings](../../evidence/m2-audio/release-process-timings.csv) cover a synthetic dense workload at four rates and seven block sizes. No missed deadlines were observed in that isolated run. At 48 kHz/64 frames, the maximum measured process call was 60.5 microseconds against a 1,333.33-microsecond period. This is an offline process benchmark, not a device callback, whole-product CPU result or realtime guarantee. It excludes UI/GPU load, device/host overhead and multiple instances.

[Listening instructions](M2_LISTENING.md) and the deterministic corpus generator provide sparse/dense, level-matched synthetic A/B comparisons against a sine reference. The final build has its own corpus under `build/m2-listening-c02b5d1886fb/`. No human listening result, wildlife recording quality, ecological interpretation, final timbre approval or held-out musical validation is claimed.

## Remaining gates

1. Listen to the first-voice comparisons and recorded-data renders; record musical decisions before choosing a broader voice palette. The provisional resource settings are not product ceilings.
2. Implement native device playback and a bounded cross-thread command/plan publication and retirement protocol. Current renderer controls require one owner between process calls.
3. Connect UI transport, voice editing, persistence and render jobs only when their native implementations and capability contracts exist; run end-to-end state/recall tests.
4. Measure actual callback deadlines and complete product CPU/GPU with visible/hidden UI, device changes, sustained playback and multiple instances. JUCE, Windows/Linux, actual Ableton/VST3 and clean-machine packaging remain unqualified.
5. Continue source work separately: preserve acoustic detections and prioritise genuine identified-animal history. [Offline data pilots](../../data-pilots/REVIEW.md) are not activated collectors. Their rights, privacy, identity joins and reliability gates remain explicit.

The applicable Field DSP V2 record is deliberately `offline_verified`. Hardware, callback measurements, listening and product delivery are unrun; the record does not admit this experiment as release-ready software.
