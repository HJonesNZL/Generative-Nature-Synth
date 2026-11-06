# Sound-design research and practice

This project uses synthesis, composition, bioacoustics, auditory perception and sonification literature alongside relevant Field engineering guidance. No skill defines the final instrument's musical scope. This is a maintained research base and working method, not a claim that an AI has acquired permanent hearing ability or mastered an exhaustive literature.

## Research map and access record

- [Synthesis methods](SYNTHESIS_METHODS_RESEARCH.md): additive/subtractive, FM/PM, wavetable, modal/waveguide, formants, granular/spectral and stochastic approaches, with costs and limitations. The six initial characters are an implementation experiment, not six exhaustive categories of sound.
- [NZ species dossier](species-nz-research.md) and [global species dossier](species-global-research.md): exact-taxon recording references, taxonomy/rights constraints, evidence versus original musical ideas; structured records live in `design/presets/`.
- [MIT OCW sound design](https://ocw.mit.edu/courses/21m-380-music-and-technology-sound-design-spring-2016/pages/readings-and-assignments/): public syllabus and assignments consulted for an analysis–model–prototype–evaluation workflow. This does not mean the paid Farnell book or every linked example was read or heard.
- [Grond and Berger, parameter mapping sonification](https://sonification.de/handbook/chapters/chapter15/): public chapter page and examples reviewed; its distinction between data properties, synthesis parameters and perceptual mappings informs our mapping specification. Linked audio was not auditioned in this pass.
- [McAdams 2019](https://www.mcgill.ca/mpcl/files/mpcl/mcadams_2019_timbreacoustperceptcogn_ch8.pdf): open author chapter consulted on timbre, grouping and orchestration. Similarity, onset relationships and context affect whether parts blend or separate; a different spectrum alone does not establish species recognisability.
- [Hagiwara et al. 2022](https://arxiv.org/html/2210.10857v1): full short paper reviewed. Animal-inspired synthesis is feasible with simple controllable components, but its evaluation does not validate our presets. We are not adding its optimisation model or a machine-learning dependency.
- [Ableton Learning Music](https://learningmusic.ableton.com/): free curriculum inspected for pitch, rhythm, motifs, harmony and structure. Browser-interactive exercises were not completed. Learning Synths was inaccessible as readable content; no mastery claim follows from its landing page.

The linked dossiers record additional primary papers, author books and recording archives. Sound On Sound access failed; a Berklee public syllabus and MIT Press publisher overview were only orientation, not substitutes for reading the paid materials. No paid course, service or reference-audio purchase is required.

## Working method for every species family

1. Establish exact taxon and the actual event semantics. Identify recording context, call type, sex/age/region where available. Record access and reuse permission separately. Do not manufacture a vocal repertoire for silent or insufficiently identified animals.
2. Describe audible hypotheses in plain terms: tonal/noisy balance, spectral shape, onset, decay, repetition, contour, phrase organisation and variability. A webpage description is not a listening observation, and an instrument control value is not a measured animal frequency.
3. Choose two contrasting musical interpretations before over-refining one. Preserve recognisable gestures while translating to a useful register, constrained harmony and purposeful dynamics. Related species may share a sound family; cousins still need salient distinguishing cues.
4. Compare synthesis approaches against the musical task. Prefer the simplest model that reaches the intended sound, not the smallest model irrespective of sound. Record alternatives and the reason for rejecting them. Prototype outside the realtime callback if complexity is uncertain.
5. Separate identity from expression. A species signature should survive transposition, soft dynamics and a changed region. Region effects support space and atmosphere; they must not erase identification or imply that station detections are moving individuals.
6. Design phrase behaviour and orchestration as well as one-shots: sparse/dense scenes, repetition fatigue, foreground/background, overlapping calls, silence and uncertainty. Use deterministic seeds and explicit density decisions so a saved performance is reproducible.
7. Evaluate matched excerpts at comparable perceived level, mono and stereo, in isolation and mixed with neighbouring families. Include unfamiliar listeners; ask both species-family recognition and musical usefulness. Use randomised order and repeat trials. Do not call numerical waveform difference an audible distinction or an approved sound.
8. Measure finite output, peak/headroom, decay, alias-sensitive cases, block partitioning, saved-state recall and callback deadlines. Measurements support listening, not replace it. Admit only the measured scope and preserve exact source/artifact identities.

## Listening and analysis protocol still to execute

The current references have **not been listened to by this agent**. Next audition sessions should cover multiple recordings per supported exact species and multiple contexts where available. Record which excerpts were actually heard, feature observations and uncertainty; avoid scraping or redistributing copyrighted audio. A browser/public archive access route may permit listening without permitting bundling.

For candidate comparison, use the same melody, note spacing, velocity/gain and render rate first; then a representative captured ecological scene. Compare attack, spectral balance, beating/roughness, modulation contour, decay and density masking. Loudness matching is a listening aid, not permission to normalize every factory patch to peaks. Keep safe headroom and report the playback level.

Owner/listener feedback should score musicality, family differentiation, species association, fatigue and suitability in ensembles. Unresolved taxa and ecological metaphors remain explicitly labelled. No recording research alone proves a listener can recognise a synthesised animal.

## Broader design backlog, deliberately not capped by this milestone

- Multi-segment and paired-note motifs, species-specific rhythmic grammar, register roles and controlled variation.
- FM/PM voices with justified alias control; richer modal/resonant instruments; actual noise/filter and formant sources where the brief calls for them.
- Wavetable and granular/spectral methods when a licensing-compatible source and clear expressive advantage exist.
- Slow environmental textures, predator/large-animal bass roles, insects/amphibians/marine mammals as verified data sources expand; do not infer species support from a synthesis capability.
- Regional ambience, delay/reverb/filter networks with bounded tails, routing and recall, plus scene-level dynamics and spatial orchestration.
- Explicit mapping profiles, performance controls, host automation and scalable CPU qualification across multiple plugin instances.

Each addition requires a documented musical benefit, bounded state/cost, reproducible saved representation and relevant evaluation. None is silently claimed implemented by the initial candidate bank.
