# Autonomous composition and spatial milestones

7 November 2026. Owner authorises two consecutive milestones, with verified commits to beta-1 after each. No paid dependency/service, owner collector mutation, or publication beyond the authorised repository is required.

## Milestone one: musical composition

Implement deterministic species phrases with key/scale, tempo, register roles, density, variation and seed. Every emitted note retains its source event index; notes are musical transformations, never extra animal observations. Original timestamps, species/individual identity, uncertainty and provenance stay intact. Scale-constrained transposition keeps edited voices in key while the neutral disabled mode preserves historical PCM. Compile/sort/admit bounded actions on the worker, not the callback.

Provide native capability-gated UI settings with generation checks, quiet preparation and explicit application. Capture settings with all voices in schema2 scenes; load schema1 unchanged and preserve its original content hash and sound. Tests cover absolute musical expectations, malformed controls, source-independent recall/render, stale interactions, blocks/sample rates and resource limits. Investigate the earlier scalar timing misses and alias-sensitive signal error. Render sparse/dense candidate examples for listening.

Completion means implemented and independently verified engineering behaviour. Reference/owner listening and physical-device/DAW qualification remain separate, visible gates; an autonomous agent must not invent subjective approval.

## Milestone two outline: regional effects and spatial mixing

After the composition checkpoint, add user-authored geographic regions and per-region stereo placement, level, echo/ambience colour. Routing uses reported positions only: station detections use station positions, identified-animal locations retain individual provenance, and missing positions remain unassigned. A rectangle crossing the dateline is supported; overlap priority is explicit. These are artistic regions, not inferred habitats or measured acoustic spaces.

Bound eight regions. Compile route/pan into each musical action off-thread; capture the route at note onset so tails cannot jump when another event arrives. Use fixed prepared delay/filter resources with bounded feedback, independent send/return, explicit tails, O(1) logical resets, no callback allocation and identical live/offline processing. Prefer a small independently derived echo and parallel-comb ambience baseline over an unbounded effects graph. Listening may motivate richer diffusion later.

Expose region configuration in Sound Studio and saved scene state. New schema3 bundles preserve regions/effects; schema1/2 playback stays unchanged. Validate geographic boundary rules, impulse/decay equations, ordinary-level headroom, stereo/mono consequences, silence/reset/seek, dense routing, memory/CPU, saved/export parity and browser workflows. End with a second push and updated release/readiness limits.

## Research and design sources

The existing exact-taxon dossiers remain the evidence base for musical hypotheses. [NZ Birds Online morepork](https://www.nzbirdsonline.org.nz/species/morepork) and [DOC recording catalogue](https://www.doc.govt.nz/nature/native-animals/birds/bird-songs-and-calls) were checked as primary reference routes. Their descriptions are not agent listening observations. No reference recording was downloaded or auditioned in this pass; no reliable audio-listening tool is available in this session.

[Parameter Mapping Sonification](https://sonification.de/handbook/chapters/chapter15/) describes mapping data to synthesis/perceptual domains; selected page examples were read, not its audio. [Smith on Schroeder reverberators](https://www.dsprelated.com/freebooks/pasp/Schroeder_Reverberators.html) was read for generic parallel-comb/allpass concepts and their limitations. Historical diagrams/constants/code are not adopted. [Artificial Reverberation](https://www.dsprelated.com/freebooks/pasp/Artificial_Reverberation.html) introduction was read for direct/early/late path distinctions, not its complete chapter. Per-region effects will be original musical settings, with mathematical bounds and impulse checks independent of natural-environment claims.

## Completion record

Composition was committed and pushed as `4eb236b`. The second scoped regional engineering milestone is implemented and verified; current behavior, exact artifacts, tests, timing outliers and remaining release gates are recorded in [regional qualification](../../evidence/spatial/qualification.md). Both milestones retain separate listening/device acceptance. No third implementation milestone was started in this two-milestone task.
