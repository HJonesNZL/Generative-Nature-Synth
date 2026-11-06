# New Zealand exact-species sound research and preset hypotheses

Prepared 7 November 2026. Research/design only; no production changes and no reference audio auditioned. The companion [structured research record](../../design/presets/species-nz-research.json) contains 11 exact requested species, 34 selected recording references, direct media URLs, contextual metadata, source access depth and provisional current-control values.

This is a musical representation project. Original synthesis should borrow perceptual organisation from a range of documented vocal contexts; it should not reproduce a recording. These are proposed starting sounds for owner listening, not approved presets or measured reconstructions.

## Evidence and access

The exact-species pages' identity, vocal description and recording sections were read through web browsing; selected recording metadata was inspected from the same public pages. NZ Birds Online is an institutional partnership of Te Papa, Birds New Zealand and DOC ([Te Papa's project description](https://www.tepapa.govt.nz/digital-museum/explore-digital-museum/science/birds-new-zealand/new-zealand-birds-online)). Every entry records its exact page URL; none relies on a genus/order description in place of species evidence.

No MP3 was fetched, played, downloaded, spectrally analysed or packaged. Direct media URLs were exposed by the page; their media responses have not been independently tested. Thus recording availability means a provider-listed public reference, not proven media playback. Frequency, duration, spectral, loudness and noise-ratio values remain unmeasured. All synthesizer values below are original musical choices. The source's dates repeatedly use day 02, so raw dates are preserved with unverified day precision, rather than converted into confident exact observation dates.

The [source copyright page](https://www.nzbirdsonline.org.nz/copyright) was read: limited research/private access differs from reuse permission. Commercial reproduction and unauthorized mining are restricted. The selected links and short research paraphrases do not license an audio library or distributable metadata corpus. No source sound should ship with the instrument on this evidence. Any later audio-derived implementation work needs its own permitted-access and provenance decision.

## Architecture implications

Use a small set of shared synthesis primitives with meaningful temporal behaviour, and keep the species data separate from the engine. Biological relatives may share a core, but taxonomy alone does not determine an audible preset. Bellbird and tūī are the most useful related-pair test: a shared ringing core can establish kinship, while phrase-level coarse interruptions should distinguish tūī. Context matters: bellbird alarm and Auckland Islands grating references prevent a rule that every bellbird utterance is a pure bell.

Five working character descriptions cover this subset: rounded hoot, liquid whistle, bright ring, dry chatter, and rough reed. They are musical design vocabulary, not a product ceiling, measured classification, or reason to force every species into an identical architecture. Further species and audition may demand other techniques. A single character's static harmonic weights will not cover these briefs.

The smallest plausible common feature set is a harmonic core with independently changing partial amplitudes, a continuous pitch trajectory, bounded amplitude/pitch oscillation, and optional band-limited noise or roughness. A fixed-capacity phrase representation can assign onset, duration, relative pitch, amplitude and articulation to each cell. Compile phrase data off the audio thread, retain explicit reset and retirement, and derive timing from absolute sample position. This memo does not specify the complete DSP state, antialiasing or runtime contract.

Three candidate routes remain experiments: (1) additive tone plus contour/phrase, (2) additive tone plus filtered noise and bounded roughness segments, (3) a richer physical/formant or modulation mechanism if the first two fail listening. The simplest successful route should win by level-matched musical evidence. Mechanism research must remain broader than the present three-partial struck engine or these working characters.

## Provisional preset starts

Tuning is an offset to the instrument's mapped note, not an animal's absolute vocal pitch. Attack/decay use seconds, gain uses dB. These settings are usable within the reported four-control range; they alone cannot provide the proposed identity.

| Exact species | Musical role / shared character | Tuning | Attack | Decay | Gain |
|---|---|---:|---:|---:|---:|
| Ninox novaeseelandiae — Morepork | Rounded low answering pair / rounded hoot | -12 | .025 | .65 | -14 |
| Todiramphus sanctus — Sacred Kingfisher | Dry insistent accent / dry chatter | +1 | .003 | .15 | -18 |
| Zosterops lateralis — Silvereye | Light liquid upper ornament / liquid whistle | +10 | .008 | .24 | -22 |
| Rhipidura fuliginosa — NZ Fantail | Fine clipped repeated cells / dry chatter | +8 | .003 | .12 | -21 |
| Anthornis melanura — Bellbird | Clear lingering melodic anchor / bright ring | +3 | .006 | .85 | -18 |
| Gerygone igata — Grey Warbler | Thin sustained filigree / liquid whistle | +9 | .012 | .55 | -22 |
| Prosthemadera novaeseelandiae — Tūī | Ringing foreground with coarse interruptions / bright ring | -1 | .006 | .65 | -19 |
| Mohoua albicilla — Whitehead | Chirps moving into a brighter tail / dry chatter | +6 | .004 | .18 | -21 |
| Nestor meridionalis — Kākā | Coarse body with a whistle alternative / rough reed | -7 | .008 | .32 | -20 |
| Petroica macrocephala — Tomtit | Compact resolving motif / liquid whistle | +5 | .008 | .36 | -21 |
| Chrysococcyx lucidus — Shining Bronze Cuckoo | Repeated upward whistles and falling close / liquid whistle | +4 | .018 | .48 | -20 |

Exact taxonomic scope and variation notes belong with every preset. In particular the supplied/NZBO name Chrysococcyx lucidus has the explicit alternate combination Chalcites lucidus ([BirdLife taxonomy reference](https://datazone.birdlife.org/species/factsheet/shining-bronze-cuckoo-chalcites-lucidus), taxonomy statement read in search result, full account not read). Australian boobooks and Australian grey fantails must not silently substitute for the requested New Zealand taxa. North/South/Chatham labels indicate source population context; they do not establish individual identity or new taxa.

## Listening and verification gate

First audition multiple permitted source contexts per species and record playback, observations and any background interference honestly. Keep the existing no-audition state until that happens. The catalog includes adult, juvenile, song, contact, alarm, flight, courtship, dialect and ensemble contexts where available; no one recording is the entire species.

Render an original common phrase across all presets at matched loudness and in both a common register and the proposed default register. Then render the proposed species gestures. Compare unrelated contrasts (morepork/kingfisher/kākā), related contrast (bellbird/tūī), and close-role contrasts (silvereye/grey warbler/tomtit/cuckoo; fantail/whitehead). First judge whether there is an audible and musically useful distinction, then whether the intended source character is evoked. Do not use an exact-bird-identification score as the only success criterion for an abstract instrument.

Ablate register, timbre, contour, phrase and roughness separately. If a sound is distinct only because it is an octave higher or louder, redesign its spectral/temporal identity. Dense simultaneous notes must retain useful species contrast without every voice competing as a foreground soloist. Ask the owner to judge musicality, recognisable broad character, related-family cohesion, distinct species gestures and fatigue.

Engineering qualification follows any chosen mechanism: bounded work/state; finite output; alias policy at highest mapped notes; no clicks from articulation switches; repeatability across block partitions; reset/panic/silence; sample-rate changes; overload/voice limits; actual DAW listening. No such implementation, measured audio, host test or owner decision is claimed by this research.

Relevant portions of Field Audio DSP were used for evidence separation and discriminating listening experiments. No Field/Eva effect-bank, six-string, wet/dry, or hardware assumptions apply.
