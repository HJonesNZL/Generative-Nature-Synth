# Animal Atlas agent rules

These rules apply now to this project. Read only the design/API documents relevant to the change; do not reload the whole repository before routine edits.

## Owner requirements

- Production logic is portable C++20. Keep the event/session/mapping/DSP core independent of JUCE and React; platform, storage, network and UI integrations are adapters. Python is a temporary reference/collector during the migration, not a second production engine.
- No paid data, hosting, subscriptions or required paid dependencies. Track technical access separately from reuse permission. Do not promise a source or archive that has not been verified.
- Preserve genuine event provenance, uncertainty, station/individual/deployment distinctions and explicit live/history/import/demo context. Never fabricate missing animal events.
- Do not stop or modify the owner's active recording/collector/watch processes or write test fixtures to the owner's database. Tests use disposable state. Read-only inspection and isolated tests are authorised without repeated confirmation.
- Open `.als` and `.amxd` only via Finder right-click → Open With → **Ableton Live 12 Suite**, choosing the entry without `_updated`. Never use default Open or double-click for those formats.

## Data streams — owner instruction, 7 November 2026

- Preserve the live listening/acoustic-detection stream as its own capability. Do not impose its station-based semantics on other sources.
- Prioritise separate streams for reported locations/history of identified individual animals. Preserve genuine individual and deployment identifiers, actual observation times and uncertainty; show tracks only when the source supports identity and ordering. Do not invent individuals, locations or continuous movement.
- Generic species-occurrence feeds remain optional/deferred research, not substitutes for the requested individual-tracking streams. Source-specific freshness, historical availability, privacy and licence constraints must remain visible.

## DSP skills — owner scope clarification, 7 November 2026

- Apply only the relevant engineering/research/evaluation parts of Field Audio DSP and Field DSP V2. They are effect-oriented guidance, not the scope or architecture of this synthesizer. V2 supersedes V1 only for overlapping applicable methods.
- Do not import unrelated Field/Eva hardware, six-string, dry/wet or effect-bank assumptions. Do not let skill templates narrow research, synthesis families, modulation, species/individual mappings, regional effects, historical composition or the complete instrument plan.
- A minimal first voice/resource setting is an engineering milestone experiment, not a final product ceiling or approved sound. Keep the full synth targets visible and distinguish engineering evidence from owner listening decisions.

## Parallel work — owner instruction, 7 November 2026

- Use subagents for independent workstreams when it improves speed/efficiency. Subagents must run on GPT-6.1 SOL. Assign disjoint file ownership and integrate their results with shared verification.

## Scope and code clarity

1. Make the smallest complete change that satisfies the task. Fix the cause and remove superseded code in the touched path. No speculative features, unused helpers, duplicate engines, compatibility layers without an actual consumer, or frameworks for hypothetical future needs.
2. Before a nontrivial component change, state its behaviour, inputs/outputs, ownership/thread, capacity/failure policy and verification briefly in the task or relevant specification. Do not add boilerplate contracts/comments to every function.
3. Use names that communicate purpose and units; keep functions responsible for one operation. Prefer straightforward control flow and early failure exits. Do not compress logic into clever one-liners or split it into arbitrary wrappers to satisfy a line-count target.
4. Prefer standard library types/algorithms and RAII. Use scoped values and unique ownership by default; sharing requires a real lifetime need. Raw pointers/views are non-owning. Wrap C resource lifetimes. Manual allocation belongs only in an explicitly justified boundary or test probe.
5. Initialise state, use `const` for unchanged values, make interfaces explicit, and validate external data at boundaries. Keep implementation details private. Introduce templates, inheritance, macros or dependencies only for a present requirement they simplify.
6. Keep errors observable. Do not catch an error and return a fabricated success/empty valid result. Workers may isolate failures with a typed/redacted failure state. Exceptions are allowed off the audio thread; callback paths must not throw.
7. Use `.clang-format` for touched C++ when the formatter is available; preserve surrounding style otherwise. Do not perform an unrelated repository-wide reformat or suppress warnings to make a build pass.

## Audio and performance

8. Audio callbacks do no allocation/deallocation, filesystem/network/SQLite/JSON work, logging, UI calls, mutexes, waiting/spinning or exceptions. Bounded work and preallocated capacity are required; `noexcept` and an allocation test alone do not prove realtime safety.
9. Compile compact immutable numeric musical actions and region routing off-thread. Strings, provider records, polygons, sorting and graph construction stay out of the callback. Define publication AND retirement so the callback never destroys the last owner of a plan.
10. Admission/grouping must be independent of host block partitioning for reproducible renders. Report overload; releases/panic cannot depend on a discardable note-on queue. Define transport generations, seek/loop/reset, note release/stealing and effect-tail ownership before integrating voices.
11. Before implementing DSP, specify its state/update order, parameter units/ranges/smoothing, finite-state recovery, sample-rate/block changes, latency, feedback/decay bounds and listening/measurement criteria. Use established DSP methods with recorded derivation/provenance; an untested mechanism is an experiment.
12. Optimise only a demonstrated cost; show the workload and before/after measurement. Avoid needless copying and repeated work. Bound voices, event actions, region buses, tails, transitions, caches and map animation. Account for every plug-in instance and the visible/hidden UI when qualifying CPU/GPU use.

## Verification and reporting

13. Bug fixes need a reproducer and a regression check. Test observable behaviour/invariants, not a copy of the implementation. Do not weaken tests, turn release assertions off, or conceal skipped cases to claim success.
14. Build changed C++ with project warnings enabled and treat project warnings as errors. Run affected tests; use sanitizers for relevant memory/UB changes and Release builds for performance. Broaden testing only for an identified risk or failure.
15. Preserve formats/provenance across migration. A parity test needs independent expected semantics and malformed/adversarial fixtures as well as comparison with Python; matching two implementations is not proof they are correct.
16. Finish with what changed, evidence, unresolved risks and unrun checks. Distinguish implemented, designed, live-tested, audio-tested and actual DAW-tested. Do not call a scheduler an audio engine or a synthetic benchmark a product CPU result.

## Commands and references

- Native build/test: `cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Debug`, `cmake --build build/native -j 4`, `ctest --test-dir build/native --output-on-failure`.
- Sanitizers: separate build with `-DATLAS_SANITIZERS=ON`. Performance: separate `-DCMAKE_BUILD_TYPE=Release` build.
- Python reference tests: `python3 -B -m unittest discover -s tests -v` (temporary fixtures/loopback; no external providers).
- Native parity: `python3 -B tests/native_compatibility.py` (existing library read-only; no provider requests).
- Live smoke scripts are explicit network tests, not routine unit checks; never run one against an occupied owner state directory.
- Sources and rule rationale: `docs/engineering/cpp-agent-rules-rationale.md`. Current prioritised gaps and build gates: `docs/audits/AUDIT_AND_REVISED_BUILD_PLAN.md`.

## Canonical workspace — 7 November 2026

All future Animal Atlas / Generative Nature Synth work belongs in `/Users/henryjones/Documents/ChatGPT/Generative Nature Synth`. Run commands from this folder. The previous Other 2/AnimalAtlas location is a relocation notice only. Preserve existing design-reference material. Old build artifacts are archived under `build/pre-relocation-2026-11-07`; configure fresh builds in `build/native`.

## Sound design — owner instruction, 7 November 2026

Use the broader research workflow in `docs/sound-design/RESEARCH_AND_DESIGN_PRACTICE.md`, not Field DSP alone. Consult primary synthesis/perception/bioacoustics sources and exact-taxon recordings; retain access, rights, evidence and uncertainty. Distinguish read descriptions, actual listening, original musical hypotheses, implemented candidates and listener-approved sounds. Evaluate phrases and ensembles as well as isolated timbres. Do not cap the instrument at the initial synthesis characters or claim waveform difference proves audible recognition.
