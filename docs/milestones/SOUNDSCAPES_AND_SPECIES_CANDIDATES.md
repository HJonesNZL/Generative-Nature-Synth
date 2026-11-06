# Complete soundscapes and species candidates

7 November 2026. Experimental integration milestone on `beta-1`; owner listening and commercial-release qualification remain open.

## Delivered behaviour

The two-screen UI now has a persistent soundscape bar. Prepare a saved piece, edit species voices, name the soundscape and Save. Capture drains acknowledged edits, refuses unfinished native gestures and writes an immutable `.atlas-scene` bundle. Open restores copied material, exact rational speed/time basis, original time bounds, global synthesis settings and all species/fallback recipes. It stops audio and never autoplays. Recalled drafts do not overwrite separately saved voice presets. A later speed change preserves current voice edits; another explicit Open restores the captured recipe.

Exports use the selected **saved** scene, not mutable drafts. One background native render job produces a 24-bit stereo WAV at the captured sample rate (currently 48 kHz from UI preparation), with progress, cancellation and safe download. Current output is dual mono; spatial stereo design is not implied. No source-library read or provider request is needed after capture. The CLI `scene-render` provides the same self-contained engine path. The UI's existing per-species Save remains independent.

Thirty-two named taxa receive original candidate settings; unresolved Chiroptera receives a clearly labelled neutral fallback. Thirty-one are recording-research candidates and great white is a nonvocal ecological metaphor. Existing saved voices remain authoritative. There are six initial additive spectral characters and eight controls: transpose, attack, decay, gain, brightness, AM roughness, pulse rate and pitch sweep. These are expressive starting points, not approved factory sounds or a limit on future synthesis methods. The public evidence labels do not claim listening occurred.

## Engineering boundaries

- Portable C++20 owns synthesis, persistence, event timing, voice defaults and rendering. React owns presentation and acknowledged edit sequencing; JUCE remains the device adapter.
- Voice schema v2 stores exact physical doubles and character, with transactional migration of v1 normalized settings and revisions. Staged draft restoration performs allocation/validation before audio preparation and publishes by worker swap.
- Self-contained scene schema v1 has strict closed tables, bounded records/profiles and independently checked metadata/content hashes. It preserves original provenance and uncertainty. Hashes detect corruption; they are not signatures/authentication.
- Limits remain 50,000 copied events, 513 voice definitions, eight active plus eight retiring oscillating notes, eight partials per candidate and bounded 30-minute rendering. Only numeric prepared state enters the callback.
- Render startup checks sealed headers only; a separate worker fully validates and compiles the scene. One active job and sixteen retained terminal jobs bound resources. Completion/cancellation has an explicit atomic-publication boundary; failed or cancelled jobs never expose partial WAVs.
- App-owned directories and opaque IDs separate UI actions from arbitrary paths. Existing destinations are never overwritten. Symlinks/unsafe origins/traversal are rejected; unmanaged filenames remain observable by count. Atomic publication uses same-filesystem hard links and does not claim physical power-loss recovery.

Contracts: [storage](SOUNDSCAPE_STORAGE_CONTRACT.md), [exports](SOUNDSCAPE_EXPORT_CONTRACT.md), [voice store](NATIVE_VOICE_STORE_CONTRACT.md), [DSP](VOICE_CHARACTER_CONTRACT.md), [UI](UI_SOUNDSCAPES.md).

## Research and design

[Working research practice](../sound-design/RESEARCH_AND_DESIGN_PRACTICE.md) combines primary synthesis literature, timbre perception, orchestration, bioacoustics and sonification. The methods survey compares additive, subtractive, FM/PM, wavetable, modal/waveguide, formant, granular, spectral, noise and biomechanical approaches. Exact-taxon dossiers contain 92 recording references and explicit access/rights/listening status. No samples, trained model, paid course or paid service are required or bundled.

[Candidate bank](../sound-design/FACTORY_CANDIDATES.md) documents species bindings, original parameters and limitations. Static spectra and smooth AM do not yet implement true multi-syllable phrases, biological roughness or broadband residual noise. The next musical experiments should test phrase grammar and true filtered-noise/formant/FM alternatives where the brief demands them, rather than treating the first six characters as the whole instrument.

`Auditions/species-candidates-v1/index.html` is a local listening page generated with 33 synthetic three-note excerpts. Its manifest identifies the engine source; measurement/hash records are in [evidence](../../evidence/soundscapes/). Audio stays untracked. These renders prove availability and numerical output, not recognisability or musical approval.

## Validation and remaining gates

Final CTest: **16/16 Debug**, **16/16 ASan+UBSan**, **17/17 Release** pass. The Release suite includes real JUCE preparation/edit/save/shutdown without enabling a device. Targeted ThreadSanitizer runs for render-job concurrency and voice-edit/runtime paths also pass with no race diagnostics or suppressions. Independent DSP and storage reviews reproduced and corrected integration issues before final verification. Native tests cover original-source removal, scene corruption/schema limits, exact voice migration/restart, immutable render parity, cancellation/publication, invalid IDs, preset matching and saved-preset precedence.

Browser tests demonstrate acknowledged capture, researched preset application, scene recall without autoplay, exact repeated WAV bytes, editable speed changes, unmanaged filenames and safe download. See the UI contract for exact scenario counts. No owner database writes, provider calls or collector changes were used for tests.

The synthetic dense-character benchmark records 42 rate/block/character cases and 2,257,464 process calls. Worst case p99 is about16.3% of its nominal block deadline. **Two overruns occurred for pulse at192 kHz/32 frames** (189.917µs maximum against166.667µs). This is not hardware callback qualification; OS scheduling, thermals and other processes were uncontrolled. No whole-product CPU, multi-instance, GUI/GPU or DAW performance claim follows.

Remaining: reference listening and owner comparison; perceptual level matching and species/family recognition; high-rate alias-convergence tests; extreme tiny-modulation performance; actual device/recovery/sustained callback measurements; Windows/Linux; VST3/DAW and embedded browser packaging; region effects and phrase-level composition. Current scene recall covers implemented controls; planned effects are not silently represented as saved.
