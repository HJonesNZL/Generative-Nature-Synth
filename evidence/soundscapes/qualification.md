# Experimental integration evidence

Production source fingerprint: `67ebca4ad3ccb383f16a658342a86c71c177c3c50fff9596eccba2c14cbb1827`. Implementation commit: `e00059a782eb686dea281c3e3c6f3db1632bef26` on `beta-1`. Exact executable/artifact hashes and synthetic benchmark scope are in `artifact-and-benchmark.json`; an immutable local audio-host copy remains under `build/soundscapes-qualified-67ebca4ad3cc`. Builds use warnings as errors, C++20 and the already installed JUCE/SQLite dependencies.

- Debug:16/16 CTest; ASan+UBSan:16/16; Release:17/17. JUnit reports retain every suite result. The added CLI regression verifies PCM equality after source deletion and refusal to overwrite its output.
- Targeted ThreadSanitizer render-job and voice/runtime binaries pass without race diagnostics or suppressions; commands, dependencies, hashes and output are retained.
- Real JUCE lifecycle test prepares/edits/saves/shuts down with output disabled. Historical earlier device tests do not qualify this new artifact.
- UI evidence is summarised separately with exact tested workflows. Synthetic fixture screenshot is included; it is not live wildlife data.
- The33 local synthetic auditions have individual hashes/PCM levels and an engine manifest. They are not animal recordings, reference listening or a proof of audible recognition.
- Dense process benchmark:42 cases/2257464 calls; two timing misses at192kHz/32frames pulse. Hardware callback/whole-app CPU qualification remains open.

The admission record admits only **experimental integration**. A mechanical hash/checklist pass is not listening, sound quality, hardware or commercial release approval. Strict alias-convergence, tiny-modulation timing, cross-platform/libm identity, sustained hardware/device recovery, multi-instance DAW and packaging checks remain unrun. The full synthesis/phrase/region plan remains broader than the initial candidate bank.

No owner recordings/database/collectors were changed by testing. Research source URLs are references only; no copyrighted reference audio, paid service, model weights or downloaded code was added.
