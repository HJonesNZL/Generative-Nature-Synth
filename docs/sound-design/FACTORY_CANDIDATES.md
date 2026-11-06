# Factory candidates: experimental species bank

7 November 2026. **32 researched named taxa plus one unresolved bat-order neutral fallback. No listening-approved factory presets.** This bank contains original musical experiments, not source recordings, measured animal reconstructions, or a ceiling of six synthesis methods.

The immutable C++ catalogue is [presets.cpp](../../native/src/presets.cpp), exposed through [presets.hpp](../../native/include/atlas/presets.hpp). Each record carries exact provider identity, supplied scientific name, reviewable description, evidence status, research path and current numeric settings. The public evidence-status enum is exactly `research_candidate`, `ecological_metaphor`, or `unresolved_taxon`; all entries remain unapproved for listening. Stable IDs are `candidate:<providerSpeciesId>:v1`; lookup requires that exact ID and returns null for unknown IDs. Related taxonomy is explicit in the research: labels are not inferred from appearance, order or generic genus recordings.

VoiceStore integration uses these starting values only for species without a persisted voice. They are unsaved clean bases until an explicit Save; unsaved Revert returns to that base. Existing persisted v1/v2 settings and the generic default are preserved. Catalogue functions themselves perform no storage writes, parsing, downloads or host calls. Explicit application to a draft remains available through host integration.

Evidence: [11 New Zealand species](species-nz-research.md), [21 global/named taxa and unresolved bats](species-global-research.md), [broader synthesis methods](SYNTHESIS_METHODS_RESEARCH.md), [implemented DSP contract](../milestones/VOICE_CHARACTER_CONTRACT.md). Source descriptions and varied recording contexts support the briefs; no reference audio was auditioned in this research pass. Recording URLs are references, not licensed assets. No recordings are downloaded or included in this bank.

## What is implemented

The candidate engine uses a fixed sine bank, spectral weighting, independent bell-partial decay, event-seeded diffuse multisine phases, smooth amplitude pulses, periodic amplitude roughness and a continuous glide. All coefficients are compiled on the worker and captured at note onset. Each voice retains its earlier lifetime and .05 peak budget; total active/retiring capacity remains eight plus eight.

`air` means sparse diffuse multisine, not broadband noise. `roughness` means bounded periodic AM, not animal vocal roughness measured from recordings. `pulseHz` supplies smooth regular amplitude pulses; it does not implement unequal syllables, acceleration, a closing cadence, texture switches or per-syllable pitch resets. A single glide cannot reproduce a full changing melodic phrase. Per-record descriptions explicitly identify important pending gestures. Those are retained research/design targets, not implemented claims.

Six names describe this first shared candidate subset. Filtered noise, dynamic FM/PM, wavetable morphing, formants, modal/waveguide sources, synthetic grains and spectral methods remain in the broader research with discriminating next experiments. No principle requires a custom engine for each species, or forces all future species into these six labels.

## Candidate settings

Tuning is relative to the existing mapped note. Implemented candidate gains supersede the much quieter provisional four-control values in the research briefs; they remain original musical starts, not perceptual loudness matching. Attack/decay are seconds, sweep is semitones. Brightness/roughness are [0,1], pulse is Hz. The following values are deliberately meaningful spectral/gesture contrasts, not random tiny variations.

| Exact provider key | Species / supplied taxon | Character | Tune | Attack | Decay | Gain dB | Bright | AM rough | Pulse Hz | Sweep |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| birdweather:species:2298 | Morepork / Ruru / Ninox novaeseelandiae | whistle | -12 | 0.025 | 0.65 | -3 | 0.08 | 0 | 2.4 | -0.6 |
| birdweather:species:102 | Sacred Kingfisher / Kōtare / Todiramphus sanctus | pulse | +1 | 0.003 | 0.15 | -1 | 0.9 | 0.45 | 9 | -1 |
| birdweather:species:420 | Silvereye / Tauhou / Zosterops lateralis | whistle | +10 | 0.008 | 0.24 | -3 | 0.55 | 0.05 | 7 | +2.5 |
| birdweather:species:4456 | New Zealand Fantail / Pīwakawaka / Rhipidura fuliginosa | pulse | +8 | 0.003 | 0.12 | -1 | 0.65 | 0.12 | 12 | +1.2 |
| birdweather:species:1276 | New Zealand Bellbird / Korimako / Anthornis melanura | bell | +3 | 0.006 | 0.85 | 0 | 0.55 | 0 | 0 | +0.4 |
| birdweather:species:4452 | Grey Warbler / Riroriro / Gerygone igata | whistle | +9 | 0.012 | 0.55 | -2 | 0.3 | 0.08 | 11 | -2 |
| birdweather:species:2561 | Tūī / Prosthemadera novaeseelandiae | bell | -1 | 0.006 | 0.65 | 0 | 0.82 | 0.65 | 4 | -3.5 |
| birdweather:species:2195 | Whitehead / Pōpokotea / Mohoua albicilla | reed | +6 | 0.004 | 0.18 | 0 | 0.45 | 0.2 | 8 | +3 |
| birdweather:species:4251 | Kākā / Nestor meridionalis | reed | -7 | 0.008 | 0.32 | 0 | 0.95 | 0.9 | 5 | -5 |
| birdweather:species:4459 | Tomtit / Miromiro / Petroica macrocephala | whistle | +5 | 0.008 | 0.36 | -3 | 0.4 | 0 | 5.5 | -3 |
| birdweather:species:591 | Shining Bronze Cuckoo / Pīpīwharauroa / Chrysococcyx lucidus | whistle | +4 | 0.018 | 0.48 | -3 | 0.15 | 0 | 3.3 | +7 |
| birdweather:species:1 | House sparrow / Passer domesticus | pulse | +3 | 0.004 | 0.24 | -1 | 0.38 | 0.35 | 7 | +1 |
| birdweather:species:35 | Eurasian blackbird / Turdus merula | whistle | -2 | 0.024 | 1.15 | -3 | 0.65 | 0.06 | 0 | -3.5 |
| birdweather:species:89 | European goldfinch / Carduelis carduelis | reed | +7 | 0.006 | 0.42 | 0 | 0.55 | 0.08 | 7.5 | +3 |
| birdweather:species:294 | European greenfinch / Chloris chloris | reed | +2 | 0.012 | 0.85 | 0 | 0.9 | 0.8 | 0 | -2.5 |
| birdweather:species:25 | Chaffinch / Fringilla coelebs | reed | +4 | 0.005 | 0.36 | 0 | 0.7 | 0.25 | 11.5 | -4 |
| birdweather:species:61 | Song thrush / Turdus philomelos | whistle | +0 | 0.016 | 0.8 | -3 | 0.8 | 0.12 | 3.8 | +1.2 |
| birdweather:species:534 | Common myna / Acridotheres tristis | reed | -1 | 0.012 | 0.65 | 0 | 0.55 | 0.5 | 3.4 | -1.5 |
| birdweather:species:44 | Dunnock / Prunella modularis | whistle | +6 | 0.014 | 0.55 | -4 | 0.18 | 0.04 | 8 | +4 |
| birdweather:species:2952 | Masked lapwing / spur-winged plover / Vanellus miles | pulse | +2 | 0.003 | 0.22 | -1 | 0.95 | 0.75 | 14 | -4 |
| birdweather:species:100 | Canada goose / Branta canadensis | reed | -9 | 0.045 | 1.25 | 0 | 0.22 | 0.35 | 1.8 | -5 |
| birdweather:species:106 | Eurasian / Australian coot / Fulica atra | pulse | -5 | 0.004 | 0.34 | -1 | 0.12 | 0.6 | 5.5 | -2 |
| birdweather:species:12 | Spotted dove / Streptopelia chinensis | whistle | -7 | 0.065 | 1.8 | -3 | 0.04 | 0 | 1.25 | +0.7 |
| birdweather:species:46 | Little owl / Athene noctua | whistle | -4 | 0.03 | 1.35 | -3 | 0.12 | 0.08 | 0 | -5 |
| birdweather:species:53 | Common starling / Sturnus vulgaris | reed | +1 | 0.008 | 0.62 | 0 | 0.98 | 0.65 | 9 | +6 |
| birdweather:species:130 | Mallard / Anas platyrhynchos | reed | -6 | 0.01 | 0.48 | 0 | 0.16 | 0.7 | 5.2 | -2.5 |
| birdweather:species:1051 | Australasian swamphen / pūkeko / Porphyrio melanotus | reed | -3 | 0.009 | 0.7 | 0 | 0.65 | 0.75 | 3 | +3.8 |
| birdweather:species:113 | Yellowhammer / Emberiza citrinella | reed | +5 | 0.01 | 0.95 | 0 | 0.55 | 0.1 | 6 | +0.5 |
| birdweather:species:64 | Common pheasant / Phasianus colchicus | pulse | -8 | 0.003 | 0.4 | -1 | 0.35 | 0.85 | 6 | -4 |
| birdweather:species:928 | Indian peafowl / Pavo cristatus | reed | +0 | 0.02 | 1.2 | 0 | 0.85 | 0.55 | 1.5 | -7 |
| birdweather:species:913 | Lesser redpoll (legacy provider species) / Acanthis cabaret | reed | +8 | 0.004 | 0.26 | 0 | 0.35 | 0.2 | 13 | +4 |
| nzsharks:species:great_white_shark | Great white shark / Carcharodon carcharias | air | -12 | 0.1 | 2.8 | -2 | 0.05 | 0.05 | 0.4 | -10 |

`birdweather:species:17651` is **Chiroptera, an order with unresolved species identity**. Its entry is the exact neutral generic struck fallback. It does not assign an echolocation pattern, ultrasonic range or species-specific sound. A later exact-species detection needs its own evidence and mapping.

The great-white-shark entry is an explicit **nonvocal ecological metaphor**: slow diffuse falling tone suggesting movement scale. This review found no exact-species call repertoire or supporting recording. No other shark's sound is substituted, and the invented pulse must not be labelled heartbeat, sonar, fin beat or a continuous movement track.

The legacy redpoll entry remains bound to original provider ID913 and the supplied Acanthis cabaret label; current lumped redpoll taxonomy does not license mapping every A. flammea detection to that narrower candidate. Supplied cuckoo and spotted-dove genus names similarly remain visible with aliases in research.

## Evidence and audition gate

Tests establish catalogue identity/uniqueness, valid engine settings, unknown lookup rejection, broad character coverage, explicit ecological/unresolved statuses, retained family cores, and finite endpoint-rate rendering for every entry. Selected family/unrelated comparisons remain numerically distinct after removing tuning and gain. These checks do not prove perceptual quality or source evocation.

Useful comparisons are bellbird/tui (shared ring, coarse contrast), blackbird/song-thrush (shared whistle, flowing versus pulsed), finches (shared reed with buzz/rattle/rolling roles), goose/mallard (shared lower reed, different pulse scale), coot/swamphen, pheasant/peafowl, morepork/kingfisher, fantail/whitehead, and tomtit/cuckoo. The contrast fixtures use a common mapped note and normalise energy in the comparison metric, so simple transposition or gain cannot be their sole distinction.

The parent host integration supplies application/list/render commands and a comparison artifact; consult its current CLI help for exact syntax. A rendered file is available to hear, not a listening pass. For owner evaluation, first use a common register and matched level, then intended defaults and musical mixtures. Conceal labels during preference judgments, preserve crest reporting, and record which roles work and which need a different mechanism. Avoid presenting an abstract tone as a realistic animal call.

Pending gates: permitted reference audition across several contexts per species; owner review of actual candidate renders; high-rate converged alias comparisons; actual callback/device timing and DAW tests; multiple-segment phrase/texture experiments where required. Low-rate/high-note/upward-sweep admission removes upper partials and can reduce colour contrast. Current gains are not measured level matches. Numerical bounds and different waveforms do not waive these limitations.


### Gain correction and numerical audition levels

The earlier research starts applied -14..-28 dB of additional profile attenuation before the host's existing -18 dB master and .05 per-state scale. A deterministic 48 kHz single-event probe found named-candidate peaks approximately -58.8..-80.4 dBFS at that master, versus -45.9 dBFS for the neutral fallback. That compounded attenuation was unsuitable for useful comparison and has been removed.

Current policy: weaker multiharmonic reed/bell bodies use 0 dB; pulse bodies use -1 dB; stronger near-pure whistles use -3 dB, with deliberately soft Dunnock -4 dB and thin Grey Warbler -2 dB; the long ecological multisine uses -2 dB. These are fixed settings, not automatic gain, peak normalisation or a measured equal-loudness target. Every gain remains at or below unity, so the existing .05 per-state and .8 aggregate proof is unchanged. The first-half-second RMS probe includes envelope/tail silence and is not an integrated loudness measurement. Owner listening should still judge balance and arrangement.

For the same deterministic 48 kHz single-event probe, applying the revised fixed gains and -18 dB master to the measured unity-gain peaks gives named-candidate peaks **-56.44..-46.68 dBFS** (neutral fallback -45.87 dBFS). Examples: Morepork -47.84, Bellbird -46.68, Mallard -56.44, ecological shark -49.00 dBFS. This arithmetic follows the renderer's linear gain path; it is neither peak normalisation nor perceived loudness matching. Source recordings and actual playback hardware were not involved.
