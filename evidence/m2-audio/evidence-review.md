# M2 documentation and evidence review

7 November 2026 NZDT. Read-only review of the current source, saved evidence, milestone contracts, launchers and source-expansion memo. No owner database/collector access, playback, provider harvest, benchmark rerun or product edit. This review distinguishes consistency checks from proof of musical or real-time quality.

## Findings for integration

1. **Launcher wording:** `Start Atlas UI.command` currently prints “read-only UI preview, no audio engine yet”. That is now misleading because the offline native renderer exists. Recommend “read-only UI preview; in-app audio controls unavailable”, consistent with the UI banner. This is a copy issue, not a missing engine or a reason to rerun DSP qualification.
2. **Artifact identity scope:** `build.json` identifies a copied qualified executable and a CMake production-source digest, not the benchmark executable or every test/script. The digest hashes nineteen sorted production/CMake inputs; test source hashes are separately present in `source-before.json`. Link benchmark results to their Release build/run records, and do not imply that the qualified Debug executable is the measured Release benchmark artifact.
3. **Reproduction scope:** `M2_LISTENING.md` gives `build/native/atlas-native` as a convenient generation command, while the retained material was generated with `build/m2-qualified-743cadf5898d/atlas-native`. Cite the latter plus its hash when describing these exact excerpts. Identical-build reproduction claims do not establish cross-compiler/platform byte identity.
4. **Evidence depth:** native command records/logs show successful build and six CTest suites per configuration. They do not expose each successful Python/C++ assertion in the log. The 13 Python render tests and explicit C++ `check` mechanism are visible in source; call these six suites, not six total test cases. UI evidence reports twelve unit tests and nine grouped browser checks, consistent with current docs.

No incorrect all-world/completed-product/listening-accepted claim was found in the reviewed current milestone documents. The historical UI handoff document describes the earlier integration slice and should remain dated evidence rather than be cited as the current absence of an offline engine.

## Mechanical checks performed

- Recomputed all twenty-five entries of `source-before.json`: all matched current files at review time.
- Reconstructed the CMake nineteen-input sorted `relative-path:sha256` manifest: digest `743cadf5898d60db03609489d5a1db920dcf3997ef7f852151d80e3fdec916cb`, matching `build.json` and listening evidence.
- Hashed `build/m2-qualified-743cadf5898d/atlas-native`: `af0411290a858472cd96ea86b61d4032a54a28e2b4804864017c71090929aec5`, matching the recorded executable hash.
- Recomputed all fourteen source/doc hashes in `globe-checks.json`: all matched. Fresh native browser report and prior static report hashes also matched. The static run is honestly marked not repeated for final pure helper changes; strict TypeScript, native/demo builds and CSP/build checks were repeated.
- Listening manifest hash matched `45327b1c2d902a4bf557c78ea37f399a3f08c54adcaca304e6c11a18aa9a8460`; all eight named WAV hashes matched its manifest. This verifies retained-file identity, not subjective audibility, playback or every waveform sample.
- Inspected Debug/Release/sanitized logs and command records: all six CTest suites passed in each. Current CMake caches identify Debug, Release and Debug+ASan/UBSan respectively, warnings-as-errors ON and Homebrew SQLite. Successful logs contain no project-warning diagnostics. Cache inspection supports configuration consistency; it is not an independent clean rebuild.
- Parsed twenty-eight synthetic benchmark rows (four rates × seven blocks), summed zero recorded deadline misses and found worst recorded `max_us/deadline_us` about 0.20475. The benchmark source uses a bounded dense synthetic timeline and times scalar process calls, excluding device, GUI, DAW and host contention. Its nearest-rank percentiles/maxima are finite-run measurements, not guaranteed worst-case latency or complete product CPU qualification.
- `afinfo` outputs identify stereo 48 kHz 24-bit little-endian PCM, 153,600/57,600 frames and 3.2/1.2-second durations, consistent with the saved candidate measurements. Container recognition is not hardware playback.

## Claims supported and open gates

The evidence supports an implemented, mechanically tested offline saved-session-to-WAV path with an experimental voice, deterministic admission and disposable synthetic qualification, plus a bounded read-only globe preview. Source tests independently calculate the stated waveform and inspect WAV structure; Release uses explicit checks rather than disabled `assert` calls. Allocation counters test exercised C++ allocation paths; they do not alone prove every real-time property or cross-thread lifetime safety. No device callback, concurrent plan publication/retirement or DAW integration is claimed.

`M2_AUDIO_CONTRACT.md` separates design bounds from listening evidence, and `M2_LISTENING.md` correctly keeps listening/hardware `not_run`. RMS matching, synthetic peak/DC measurements and a valid file cannot establish preferable timbre, masking behavior, owner acceptance or ecological validity. The proposed neutral comparison and wider synth palette remain open.

`GLOBE_PREVIEW.md` correctly states 200 initial revisions, 2,000 retained records, sampled catalogue limits and 400 map symbols. Its twelve/five-new unit-test counts match current evidence. “All loaded” does not mean all database or planet records; source freshness and full-library summary/pagination remain gates. Fixture/browser passes do not establish owner-library behavior, native-webview rendering, GPU cost or long-duration stability.

`GLOBAL_GLOBE_SOURCE_OPTIONS.md` distinguishes species occurrences from individually tagged fixes, public access from reuse rights, and observation dates from fetch/index dates. GBIF-first and a vetted Movebank repository study are recommendations, not enabled providers or approved datasets. OBIS rate/page uncertainty and per-record licensing/privacy gates remain explicit. No extra source freshness/coverage claim should be inferred from that research.

These are review-time checks. Any later source change requires updating the corresponding digest/evidence scope; documentation-only edits do not invalidate DSP behavior but can invalidate a recorded documentation hash. Parent qualification/admission document was not yet present when this initial review was written.

Reviewer correction supplied to parent: the qualified copied executable is **Release**, not Debug. Its recorded SHA256 matches `build/native-release/atlas-native`; the Debug binary has a different hash. A convenience path was incorrectly used to infer configuration in an earlier message.
