# Native playback qualification — 7 November 2026

**Result: experimental saved-piece playback is connected from H2 controls to the C++ renderer and the default native output.** The browser owns presentation; native code owns verified preparation, timing, transport, device lifecycle and synthesis. This is a development browser plus optional JUCE output process, not a packaged standalone or VST3 release.

The final Release artifact is identified in [final-artifact.json](final-artifact.json). Its SHA-256 is `25ee4e92c82c428f9ccebb45e662701d2e39ad6a2e439d0f4ca5502921cd401e`, matching both real-output smoke receipts. It is retained locally in an immutable ignored build folder. JUCE 9.0.0 uses the existing clean checkout at recorded revision `f8f8864172464b9adf9eba6101e1f784838d1597`; no dependency was purchased or downloaded. Product distribution/licensing and packaging remain separate gates.

| Check | Observed result | Practical limit |
|---|---|---|
| Final Release | 9/9 CTest suites | Includes optional actual JUCE prepare/shutdown regression, with no audio enable |
| Final Debug and ASan/UBSan | 8/8 suites each | Portable runtime/protocol paths; JUCE device adapter not sanitizer-qualified |
| Independent ThreadSanitizer | 13/13 playback groups, no reported races | One run supports the explicit ownership proof; it is not proof of all schedules |
| Numerical and WAV behavior | Existing 11 audio groups and 13 WAV groups pass | Same experimental voice, not a broader synth/effects palette |
| Runtime and protocol | 13 playback groups and 9 protocol groups pass | Actual engine with manual/clocked test output drivers |
| Frontend | 15 units; strict TypeScript; both builds/artifact checks; 8 playback, 6 native-fixture, 8 earlier-readiness and 9 general browser groups pass | Each named browser checkpoint retains its exact source/artifact and timing scope |
| Actual device smoke | 48 kHz, fixed -18 dB; play, pause, seek, resume, stop, disable, clean EOF | Approximately 2.237 seconds total; no physical audio capture or listening judgment |
| Browser → actual device | Native position advanced; pause held at 506,333 µs; Stop and disable acknowledged | Play attempt to disable took approximately 673 ms; one foreground browser/default-device run |

Final native command receipts record unchanged source hashes across their builds/tests. Earlier results retain their original identities. The ThreadSanitizer runtime sources did not change during the later application-loop and device-list repairs. The earlier broad browser/native-fixture result is not relabelled as a run against a later executable: the separate browser-to-device receipt establishes the full repaired output path on the final artifact.

Two real-host failures were reproduced and corrected. Raw console JUCE initialization returned without an NSApplication event loop and deadlocked preparation; the host now uses public JUCEApplicationBase startup. Broad device-list invalidation then mistook JUCE's own aggregate-device notification for an output change; the listener now checks selected/default identity. Failed receipts and diagnoses are retained, followed by passing real-host and real-device regressions. No vendor source was changed.

## Controls, ownership and level bound

The specification is [NATIVE_PLAYBACK_CONTRACT.md](../../docs/milestones/NATIVE_PLAYBACK_CONTRACT.md). Output starts disabled. Preparation disables the previous output; opening a device stays silent until Play. Preparation/generation fences reject stale commands. The UI waits for actual paused state before seeking and resets a completed piece before replay. A five-second process-level lease and separate callback frame countdown silence output after lost heartbeats. One controlling UI window is supported; multiple tabs and browser throttling are explicit limitations.

The control worker owns the immutable plan and borrowing renderer. Driver stop establishes callback quiescence before replacement/destruction. The callback consumes at most one command, processes bounded buffers with fixed scratch/voices, and publishes atomic state. C++ allocation/deallocation probes, source review and TSAN support this boundary. The JUCE/OS backend contains its own internal synchronization and is not certified lock-free by these tests.

Headroom uses the unchanged M2 analytical bound: at most 16 active/retiring states, each bounded by 0.05, gives at most 0.8 per channel before master attenuation. The fixed UI playback plan applies -18 dB, giving a conservative application-sample bound of approximately 0.100715 per channel. No feedback, gain boost or extra summing was added. This is a bound on generated samples, not physical loudness or a measured output-device waveform. Native DSP/WAV tests remain the numerical evidence; the UI presents unknown peak/voice-count measurements as null.

## Unrun and remaining gates

The admission stage is **experimental_integration**. Human listening, sound-quality approval, physical speaker routing/capture, callback execution-time/deadline measurement, sustained GUI/audio load, real device unplug/default/rate changes, sleep/recovery, multiple windows/instances, Linux/Windows, embedded webview, packaged delivery and DAW/VST3 are not qualified. Brief default-device success is not the full hardware gate. UI master gain, voice editing/persistence, effects, render jobs and live sonification remain unsupported. The full synthesis and data-source scope is unchanged.

All tests used disposable synthetic data; the owner library and active collection were untouched. All task-owned UI servers and native smoke children were stopped. Advancing real-device playback occurred only in the two explicitly bounded successful output smokes; the pre-fix attempts failed before playback. Complete source, commands, receipts and independent review are retained here; generated binaries/audio remain local ignored artifacts, not distributed product builds.
