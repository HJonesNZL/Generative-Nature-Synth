# Regional mixing engineering checkpoint

Final production source fingerprint: `6ab43e1925b126a4749c7ab41923b86173cbe02386d42db066c7eabe6f94d618`. Exact native/CLI/benchmark hashes and an immutable local JUCE host copy are recorded in artifact-and-benchmark.json. Native changes build with warnings as errors. This completes the scoped implementation and engineering verification, not commercial-release, listening or device qualification.

## Delivered behaviour

Up to eight ordered user-authored rectangular regions, including the dateline and explicit first-match boundaries. Reported receiver positions stay station positions; individual fixes retain source identity and uncertainty. Missing/outside positions are unassigned. Musical note routes are compiled once and captured at onset/voice retirement.

Per-region level and fixed/longitude pan feed stereo direct sound, normalized echoes and four damped-comb ambience paths. Fixed capacities, convex feedback/headroom bounds, logical resets, quiet settings changes and an explicit 12-second wet horizon and final 20 ms fade are specified in docs/milestones/SPATIAL_DSP_CONTRACT.md. This is experimental musical ambience, not a measured environment or a claim of a diffuse commercial reverb.

Sound Studio provides ordered region editing, map bounds, all effect controls and truthful assigned/unassigned musical-note counts. Independent Apply preserves the sibling recipe draft. Save waits for acknowledged voice edits, composition and spatial settings in order using fresh preparation identities; malformed/stale/externally changed recipes are rejected. Recall remains quiet and restores complete voices/composition/regions. Schema3 preserves exact schema1/2 hashes and audio. CLI and background jobs now share one complete versioned WAV recipe and produce identical bytes for a given scene/build.

## Verification

Full Debug 19/19, ASan+UBSan 19/19 and Release 20/20 suites passed. The independent review identified a vacuous disabled-composition comparison, which was corrected; a thirteenth spatial group tests nonzero final-tail fade. Final focused Debug/Release/sanitizer receipts cover those strengthened spatial and runtime tests. Final export Debug/sanitizer receipts and full final Release 20/20 cover the shared CLI/UI metadata repair. Full original and final focused JUnit files are retained rather than relabelling earlier runs.

Thirteen spatial groups cover analytical echo and independent damped-comb impulse equations at 22.05, 48 and 192 kHz, geography/UTF8/overlap, dry/echo PCM oracles, stereo/mono bounds, onset/retiring routes, aligned historical tails, logical reset, NaN/Inf recovery, tiny-state flushing, capacity/horizon limits, block partitions, pause/seek/panic and callback allocations/frees. Exact maximum delay storage is 16,379,904 bytes per renderer at 192 kHz with eight regions. Stopped old renderer +new preparation +one offline render can overlap at 49,139,712 bytes of delay storage; plans, source records, output/UI and other instances are additional.

ThreadSanitizer passes 14 runtime groups, including 24 wet-buffer replacements while a fake callback thread runs, callback allocation/deallocation counters and quiescent retirement. The actual pre-spatial writer's schema2 file passed the new reader with its original manifest and PCM; both optional arm64 legacy PCM baselines also pass (legacy-pcm.txt). Portable content/recall/analytic checks remain unconditional.

UI: 34 unit checks, strict TypeScript, production Vite, 14 existing composition browser groups and 11 spatial browser groups pass. Real native processing uses a silent disposable fixture, not a browser audio substitute. It verifies two held acknowledgements during combined save, exact full recall/retiming, stale/malformed/external conflicts and immutable stereo WAV output. Screenshot inspection confirms the geographic editor/controls fit their scrolling panel. Commands and scope are in ui-validation.json.

## CPU and listening boundary

Two scalar Release runs, each with 28 cases, measured 1,013,715 process calls per run. Initial run: one missed nominal deadline (composed pulse, 192 kHz / 512 frames). Final run after correcting counters: three misses (composed pulse, 192 kHz / 32 frames). Both have zero misses at 48 kHz; worst p99 across rates is about 17.3% of nominal block time. Final trigger/steal counters exclude warm-up; initial counters are explicitly historical and are not admission-invariance evidence. The outliers are retained and unattributed. They are not waived or hidden by a clean rerun. No physical-device, visible-globe/DAW, multi-instance, background-tab or sustained-load deadline claim follows.

Final synthetic six-species sparse/dense regional examples and self-contained scenes are local at Auditions/regional-soundscapes/index.html. Their hashes/settings/counts are in listening-corpus.json. They are original musical candidates, not wildlife recordings. No agent listening or owner approval is claimed.

Next product milestone: an installed standalone workflow using the same native engine/local UI, deliberate device selection/recovery and sustained representative scene tests. Owner listening, source/distribution rights decisions, complete accessibility/graphics qualification and actual DAW/plugin delivery remain open. Existing collectors, owner wildlife databases and design ZIPs were not modified by this work.
