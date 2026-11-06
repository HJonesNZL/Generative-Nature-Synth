# Composition engineering checkpoint

Production source fingerprint: `e837029026158b472145d6af31cb47374861ab6e606538c86f45c3b0ca5fc60c`. Exact native/benchmark hashes are in artifact-and-benchmark.json. Local immutable JUCE audio-host copy: `build/composition-qualified-e83702902615`. Builds use warnings as errors.

Debug18/18, ASan+UBSan18/18 and Release19/19 CTest targets passed. The Release extra target verifies JUCE host lifecycle, not physical sound. Original arm64 schema1 PCM baseline passed with ATLAS_VERIFY_LEGACY_PCM_BASELINE=1; portable saved-scene/content and block checks remain unconditional. UI strict TypeScript, production build, unit checks and isolated real-native browser workflow passed; final numeric typing/invalid-save regression is recorded in ui-validation.json.

All36 independent alias-sensitive residual cases passed the predeclared -60dBr bound; worst -74.86dBr, convergence -123.10dBr, integer-reference/runtime error <=1.86e-9. See docs/sound-design/ALIAS_PROBE.md for the finite scope. This is not strict alias freedom or perceptual transparency.

Synthetic active-density composition benchmark: 18 cases, 195030 calls, 0 observed deadline misses at48k/64,256,512 across six modulated characters; worst p99 is 2.97% of nominal block time. No concurrent heavyweight test jobs ran during measurement. This does not repair or waive the prior pulse192k/32 scalar misses, qualify OS/device deadlines, or measure visible globe/DAW load. General device/host performance remains open.

Sparse/dense six-species candidate ensembles rendered with explicit synthetic provenance at Auditions/composition-v1/index.html (local ignored artifacts), alongside independent copied scenes and WAV hashes in listening-corpus.json. No recordings were heard and no subjective approval, natural-call reconstruction or species recognisability is claimed.

Delivered: deterministic bounded species phrase plans; exact key/scale/tempo/density/variation/seed settings; scale-constrained voice transposition; quiet native UI preparation and save barriers; schema2 preserving schema1 hashes/audio; immutable scene rendering and capacity/error coverage. This completes the scoped composition engineering milestone. Broader M2/product release acceptance still requires listening, device/DAW qualification and packaging.
