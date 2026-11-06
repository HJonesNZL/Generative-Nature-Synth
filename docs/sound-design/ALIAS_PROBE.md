# Same-spectrum alias-sensitive reconstruction probe

This offline engineering probe measures whether the current additive candidate voices agree with an anti-aliased reference of the **same admitted low-rate partial set**, including finite envelopes, pulse/roughness AM and moving pitch. It does not select a sound, establish species recognition or qualify a device. The relevant engineering/evaluation portions of Field Audio DSP and Field DSP V2 apply; this adds no effect or realtime dependency.

The declared targets were frozen before running the probe: runtime-versus-filtered-reference RMS error at most -60 dB relative to reference RMS (0.1% signal-error engineering target), 8x-to-16x filtered-reference convergence at most -80 dBr, and unfiltered integer-age analytic/runtime absolute error at most 2e-7. These are numerical engineering gates, not auditory masking thresholds. A failing case is reported; thresholds are not adjusted to fit measured output.

The fixtures are six nonlegacy character candidates, rates 22050/48000 Hz, sweep -12/0/+12 semitones, root 72 with the highest historical mapped pitch 88, minimum 2 ms attack and 100 ms decay, maximum brightness/roughness/pulse (1/1/16 Hz). Composition is disabled to isolate the synthesis mechanism. Coefficients come once from the low-rate AudioPlan. No high-rate AudioPlan is created, so no extra high-rate partials or changed partial normalization enter the comparison.

The independent reference evaluates raised-cosine attack/release, exponential decay/modal damping, deterministic initial phases, sinusoidal AM and the closed-form geometric sum of the sampled exponential sweep. Fractional-age continuation agrees with the runtime at integer ages; it defines the particular continuous reference used here. This continuation has a small derivative change where the sweep clamps. Finite envelopes and clamped sweeps have nonzero spectral tails; admitted carrier/AM bounds alone do not prove strict band limitation.

At 8x and 16x, a radix-2 DFT transforms a period containing the full finite voice and at least three voice durations of silence. The signal begins one quarter of the low-rate period into the buffer. An ideal zero-phase low-pass retains bins strictly below the low-rate Nyquist; its boundary bin is zeroed. The inverse DFT is decimated at aligned integer ages. The same physical period/bin cutoff is used for both rates. Comparison includes the complete padded period, including low-pass pre/post-ringing. A doubled-period reference checks sensitivity to circular boundary images. This is a converged finite-period spectral projection, not an assertion of infinite-time sinc convolution. Known periodic pass/stop tones and forward/inverse FFT checks independently test filtering and decimation.

Reported residual includes alias-sensitive finite-envelope/moving-pitch reconstruction error, float rounding and reference approximation; it is not a measurement that separates every alias component from all other numerical error. It does not cover composition gates, stealing/transport fades, all parameter combinations or audible acceptability.

Primary method references consulted: Julius O. Smith III, [Sampling Theory](https://www.dsprelated.com/freebooks/mDFT/Sampling_Theory.html), [Sampling Theorem](https://www.dsprelated.com/freebooks/mDFT/Sampling_Theorem.html), and [Filling the FFT Input Buffer](https://www.dsprelated.com/freebooks/sasp/Filling_FFT_Input_Buffer.html). These justify band-limit-before-decimation and the explicit circular/zero-padding distinction; implementation and fixture choices here are original test code. No public DSP implementation was copied.

Measured on 7 November 2026, Apple clang 17.0.0 (`clang-1700.6.4.2`), arm64 macOS, optimized standalone C++20 build with all project warnings treated as errors. All 36 cases passed the declared limits; FFT roundtrip and independent pass/stop tone checks passed. The largest integer-age absolute error was 1.86e-9.

| Low rate | Worst residual dBr | Worst 8x/16x convergence dBr | Worst doubled-period difference dBr |
| --- | ---: | ---: | ---: |
| 22050 Hz | -74.86 (reed, zero sweep) | -123.10 | -106.57 |
| 48000 Hz | -95.76 (air, +12 semitones) | -137.05 | -138.76 |

The finite-envelope residual is measurable even though admitted carrier plus AM frequencies remain below the guard band. This evidence supports this bounded experiment's -60 dBr engineering target; it does not establish strict alias freedom, an all-controls guarantee, perceptual quality, listener acceptance or device/DAW qualification. No production DSP was changed as a result.

Reproduce from the project root (all outputs are disposable):

```sh
c++ -O2 -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Werror -I native/include native/tests/alias_tests.cpp native/src/audio.cpp native/src/core.cpp native/src/composition.cpp -o /tmp/atlas-alias-tests
/tmp/atlas-alias-tests
```

The tested production-source SHA-256 values were `a2ed83da3711727906118085c81f5ba0f886b743983e64430722a239a770026b` (`audio.cpp`), `9451fa0dd37ccf76d15e856b15d36a28f1d8a1340ec1e273a3f6e149738117eb` (`core.cpp`) and `62c38af1d56b0ca773ea13a597393bd7265fc36c8d0e20c1a39578cbc3e2a0bc` (`composition.cpp`). The test retains all fixture controls, seeds, reference equations, filter bins and frozen thresholds. Parent integration separately wires and verifies the CMake/CTest target; sanitizer and whole-project evidence belongs to that integrated build.
