# Experimental voice-character contract

Specified 7 November 2026, before implementation. This is a bounded musical candidate, not an approved factory sound or a limit on future synthesis methods. See [method research](../sound-design/SYNTHESIS_METHODS_RESEARCH.md) and the exact-species research records.

## Behaviour and ownership

The existing mapped MIDI note and its species tuning remain the pitch baseline. New characters share a fixed eight-partial sine bank with independently authored spectra; brightness changes upper-partial weight. A smooth amplitude pulse, bounded amplitude roughness and a continuous glide supply generic musical gestures. They do not encode literal source recordings, biological anatomy or measured call trajectories. In particular `air` is a sparse diffuse multisine texture, not broadband noise, and `roughness` means smooth amplitude modulation, not measured animal vocal roughness.

The plan worker validates `VoiceSettings`, prepares all exponentials, gain, spectra, oscillator ratios, decay factors and sweep multipliers, and prepares an integer seed from source ID, event ID and revision. Numeric POD coefficients cross the existing mailbox. A voice captures a complete snapshot at onset; edits affect subsequent onsets and do not retune or recolour active/retiring notes. Callback processing owns its oscillator/envelope state and performs no parsing, hashing strings, allocation/free, I/O, mutexes, exponentials or random-device calls. Existing plan publication/retirement and transport ownership remain unchanged.

Inputs: scheduled note/action, worker-prepared coefficients, sample rate. Output: dual-mono bounded float audio and existing numerical fault statistics. Maximum host block remains 4096 frames, active voices eight plus eight retiring states, admission four events per absolute 10 ms window. Each partial bank has at most eight sine evaluations per active state per sample, plus at most two trigonometric gesture evaluations and existing envelope/ramp work. There is no added observation window or algorithmic audio latency. This is an operation bound, not hardware deadline qualification.

## Parameters and persistence

Existing controls retain their ranges: tuning integer -12..12 semitones, attack .002.. .100 seconds, decay .100..3 seconds, gain -60..0 dB. The effective root must still lie at MIDI 48..72; note offsets up to 16 therefore give effective unswept notes 48..88. Additional settings are `character` in {struck, whistle, reed, bell, air, pulse}, `brightness` in [0,1] default .5, `roughness` in [0,1] default zero, `pulseHz` in [0,16] default zero, and `sweepSemitones` in [-12,12] default zero. Nonfinite values and unknown enum values are invalid. Persistence/string identifiers are owned by VoiceStore integration, not duplicated in this DSP module.

Recall and undo restore authored settings, not a past oscillator state. Onset capture is the transition policy for every control. There is no hidden smoothing of existing active notes. Pause freezes the timeline and all states; resume uses the established transport ramp. Stop, panic, seek, generation rejection, oldest-note stealing and five-millisecond retirement retain their prior semantics. Sample-rate changes require a new worker-prepared plan and fresh renderer; coefficients are rate-specific and must not be shared across plans at different rates.

## Independent synthesis design

For new-bank sample n, each oscillator contributes `a_i * d_i[n] * sin(phi_i[n])`. The worker normalises weights so the sum of absolute amplitudes is at most one; each per-partial gain starts at one and multiplies a factor in (0,1]. Brightness applies an independently authored upper-partial tilt before normalisation, and does not move frequencies. Character ratios/weights are original musical experiments, not copied animal measurements or published preset tables:

- `struck`: original harmonic proportions 1, .25, .08; brightness tilts upper harmonics when nonneutral.
- `whistle`: a fundamental and very weak second/third harmonics.
- `reed`: a progressively weaker odd-harmonic ladder.
- `bell`: a nonharmonic ratio set and progressively faster upper-mode decay.
- `air`: a nonharmonic dispersed ratio set with onset phases derived from the action seed.
- `pulse`: a finite Fourier-series pulse-like spectrum with an authored duty proportion; no discontinuous square-wave evaluation.

For the exact neutral `struck` setting (brightness .5, roughness 0, pulse 0, sweep 0), dispatch to the historical three-sine expression with unchanged arithmetic/update order. All legacy four-control values follow this branch. It must produce exactly the same PCM as the prior implementation in a same-toolchain before/after comparison, in addition to the independent closed-form tolerance checks.

The envelope remains the previous cosine attack, exponential -60 dB decay, and cosine twenty-millisecond release. Let `A=ceil(attack*rate)`, `D=ceil(decay*rate)`, `R=ceil(.020*rate)`, `L=A+D+R`. The decay state starts at one, remains fixed through attack, then multiplies `exp(-ln(1000)/D)` after each sample. Bell's additional partial decay factors apply from onset and never exceed one. The lifetime does not increase: at frame L the voice is inactive, regardless of gesture or character. Transport and retiring fades multiply the existing envelope.

Pulse uses `[(1+cos(p[n]))/2]^2` when pulseHz > 0, otherwise exactly one; phase advances by `2*pi*pulseHz/rate`. This generates smooth amplitude modulation through twice the pulse frequency and starts at full amplitude. Roughness uses `1-r/2+(r/2)*cos(q[n])`, in [1-r,1], at a worker-selected 29..60 Hz rate controlled by brightness; r=0 is exactly neutral. It is regular AM, not a chaotic or stochastic source. The two gains multiply the oscillator sum and cannot increase its peak bound.

Sweep starts at frequency ratio one, reaches `s=2^(sweepSemitones/12)` at sample `A+D-1`, and holds through release. For earlier samples the ratio advances geometrically by worker-prepared `s^(1/(A+D-1))`; clamp its state into the interval between one and s and assign the endpoint exactly. Phase increments use the current ratio and then advance the ratio for the next sample. No callback exponentials or per-block control steps occur.

Onset phase distribution uses a specified nonzero 32-bit seed; the worker uses FNV-1a over source ID, delimiter, event ID and the revision's eight little-endian bytes. At onset a bounded eight-step xorshift32 sequence yields uniform integer fractions for `air` phases. It is a deterministic phase-distribution mechanism, not an entropy source or statistical noise claim. Same identity/revision and settings reproduce after replay, seek-to-onset, or different callback partitions. Other characters begin with zero phase.

## Band limits, numerical bounds and failure policy

All supported rates are 22050..192000 Hz. Prepare each partial against the conservative maximum effective note for that profile (`root+tuning+16`) and the maximum sweep ratio `max(1,s)`. Retain it only if its highest oscillator frequency plus roughness frequency plus twice pulse frequency is below .45 times the sample rate. The set remains fixed throughout a note: it cannot pop in/out while gliding. Renormalise retained weights. This deliberately sacrifices high partials before Nyquist, especially for high notes/upward sweeps at 22050 Hz. It can reduce timbre contrast and must remain visible as a design limitation.

This is oscillator/AM-sideband admission with a guard band, not proof of strict alias-free output: finite attack/release envelopes and time-varying chirps have unbounded mathematical spectral support. No abrupt oscillator reset or sampled discontinuity is introduced inside an active voice. A later audible-alias qualification needs a converged high-rate reference for the same mechanism and listening. Legacy neutral struck retains its historical spectrum unchanged.

Each sine is bounded by one, each modal decay and modulation gain by one, the normalised absolute weight sum by one, envelope by one, profile/master gain by one, and per-voice output scale is .05. Sixteen current/retiring states therefore preserve the .8 aggregate analytical bound before the existing finite-output safety check. No clipper is used to establish this bound. Callback coefficient validation rejects invalid numeric ranges, oversized banks, unnormalised weights and oscillator steps that violate the plan's frequency budget; rejection retains the prior bank. A nonfinite rendered sample clears that voice and increments faults. Global finite/headroom failure retains the existing clear-and-report behaviour.

## Verification and admission gate

Required before integration: warnings-as-errors build; independent closed-form oscillator/envelope oracle for representative static/new-gesture cases; historical PCM comparison; brightness/roughness/pulse/sweep observable-change probes; same-note character spectral differences; all supported endpoint rates/notes/control extremes; invalid settings and malicious coefficient rejection; update-at-onset; deterministic seeds/revision differences; exact partition equality; dense stealing and retirement bounds; silence/reset/seek; no callback allocation or destruction; sanitizer checks. Distinguish waveform differences from audible musical success.

Listening remains not run until actual material is auditioned. Prepare level-matched and concealed common-note comparisons, then species gestures, related-family contrast and dense musical mixtures. Compare this candidate against true filtered noise, FM/PM, moving formants, modal resonators and synthetic grains when the research predicts a useful distinction. Those methods are deferred experiments, not rejected product scope. Hardware/DAW qualification and owner sound admission remain separate gates. Record measured commands/results below after implementation; never promote an unlistened candidate as an approved factory bank.

## Evidence

Specification: complete before DSP edits. Numerical, compatibility, control, runtime, sanitizer, listening and hardware results: pending implementation at initial specification.


### Implemented verification, 7 November 2026

- The fixed bank and numeric gesture coefficients are implemented; named character defaults remain experimental. All owned C++ was formatted with Xcode's clang-format.
- Standalone Clang C++20 `-O2 -Wall -Wextra -Wpedantic -Wconversion -Werror` build and character tests pass: independent long-double closed-form signal/gesture oracle, static character contrasts after level normalisation, macro activity, deterministic event/revision phases, exact block partitions, boundary/irregular rates, extreme note/sweep admission, onset snapshots, invalid settings/POD rejection and no-allocation dense stealing. Tests also cover root-specific coefficient pitch rejection and effective transposed MIDI48/88 boundaries.
- Existing audio tests pass all eleven groups, including the earlier legacy equation, transport, retirement and allocation probes. Same-toolchain before/after capture across 22050/48000/192000 Hz, four old-control cases and dense stealing is bit-identical: PCM SHA-256 `681f20851dc93ab5623a38f2177b6a05cbdb4a2c6accffab05deb0ae46adc07b` for both files. Capture program and raw PCM remain temporary verification files, not product assets.
- AddressSanitizer plus UndefinedBehaviorSanitizer pass the new character tests. Callback allocation/free counters remain zero in all-character dense renders; this does not establish callback deadline compliance.
- Independent source-media audition, owner musical acceptance, high-rate alias-convergence comparison, actual device timing and DAW testing remain **not run**. Numeric same-note waveform separation is established; perceptual quality/source evocation is a listening hypothesis.

Reproduction: compile `native/src/core.cpp`, `native/src/audio.cpp` and `native/tests/voice_character_tests.cpp` with the flags above; add `-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g` for sanitizer qualification. Parent build integration owns the CMake target; the source itself has no platform/UI/storage dependency.
