# M2 first sound: implementation contract, revision 1

7 November 2026. This implements the next authorised milestone under the canonical plan: verified saved data → deterministic musical actions → a small scalar C++ voice pool → offline WAV and a minimal local audition launcher. It does not imply JUCE/device callback integration or owner approval of a final instrument timbre.

## Sound brief and selection

Input is a verified immutable M1 saved session, never a provider request. Sparse records should sound clear, soft-edged and intimate; bursts should remain bounded; gaps remain gaps. One synthetic struck tone makes onset, pitch, decay and level measurable. It is sonification, not a recording or reconstruction of animal calls. A three-sinusoid additive voice is chosen over a feedback pluck or noise resonator because its spectrum and envelope have a direct independent oracle. No public DSP implementation is copied.

Revision 1 maps UTF-8 species IDs through FNV-1a-32 into offsets [0,2,4,7,9,12,14,16] above root MIDI 60. Missing species uses offset 0, explicitly counted. Hash collisions are allowed musical choices, not species equivalence. All event kinds use the same experimental voice; no confidence, individual behaviour, location, abundance or ecological silence is inferred. Stereo is centred dual mono; geographic effects remain future work.

## Compilation and ownership

Compile the Timeline's stable time/revision/ID order off-thread into immutable numeric actions and a numeric per-event admission ledger. At most four events survive each absolute 10 ms **scaled playback-time** window, retaining the earliest records in the established order. This cap is independent of process block boundaries. Changing speed may change density admission; it never changes oscillator pitch. All input records remain preserved in the source session, and suppression counts/policy are embedded in the WAV recipe.

Quantise each accepted onset upward to its first sample at or after dueUs (ceil(dueUs*rate/1e6)); no early onset, <1 sample timing error. Preserve the complete requested timeline including empty windows. Extend only as needed to finish the last admitted note. Maximum 50,000 input events, 30 minutes before tails, 22,050–192,000 Hz; blocks 0–4,096 frames. Actions hold no strings/JSON/pointers to provider records. A Plan owns its arrays; a Renderer borrows a stable const Plan for its entire lifetime. Construction, destruction, plan replacement and file writing stay on the worker. No plan publication/retirement crosses threads in this milestone; a future device adapter must add that protocol rather than transfer last ownership in a callback.

## Voice equations and bounds

Parameters are fixed for a prepared plan: root MIDI 48–72; attack 2–100 ms (default 8); decay-to-minus-60-dB 100–3,000 ms (default 650); master -60–0 dB (default 0). No runtime parameter automation is offered. Rate/parameter changes require a new plan/renderer while quiescent.

Let A=ceil(attackSeconds*rate), D=ceil(decaySeconds*rate), R=ceil(0.020*rate), age n, theta[n+1]=(theta[n]+2*pi*f/rate) mod 2*pi and theta[0]=0. Before each phase update:

- attack envelope a[n]=(1-cos(pi*n/A))/2 for n<A, else 1;
- decay d[n]=exp(-ln(1000)*max(0,n-A)/D), implemented by a prepared multiplicative factor after attack;
- terminal fade r[n]=1 before A+D, else (1+cos(pi*(n-A-D)/R))/2 until n=A+D+R, then exact silence;
- waveform w[n]=(sin(theta)+0.25*sin(2*theta)+0.08*sin(3*theta))/1.33;
- per-voice sample v[n]=0.05*a[n]*d[n]*r[n]*w[n].

Eight primary voices, plus eight retiring voices. Free primary slot first; otherwise steal the oldest onset, then lowest slot, retaining the old state for a 5 ms cosine fade. At most eight admissions occur in any 5 ms interval under the window policy, so eight retirement slots suffice. Failure of that invariant is an observable fault, never silent unbounded growth. Every note stores its own finite lifetime; releases do not depend on an event queue. Voice maximum absolute value ≤0.05, all 16 states ≤0.8 per output channel before master attenuation. Master never boosts. No limiter, clipping, oversampling, random synthesis, feedback, delayed audio edge or hidden lookahead. First sample at an onset is zero because phase/envelope start at zero; this is the attack, not added algorithmic buffering. Sinusoidal carriers stay below Nyquist at the admitted settings; this is not a claim of mathematically zero envelope sidebands.

Clear an affected voice and increment a fault counter on non-finite state/output. Reject non-finite settings before construction. Finite lifetimes cut all states long before denormal decay. Each process sample scans at most 16 voices and consumes at most four actions at a sample (eight if upward quantisation merges adjacent admission windows at a boundary). No allocation/free, IO, locks, exceptions, database, logs, UI or sorting in process. Zero-size blocks are no-ops; invalid nonempty buffers return false unchanged so the adapter must provide its own silence policy.

## Transport subset

One owner calls controls between process calls; these are not cross-thread APIs. Ready → play → playing; natural completion → finished. Pause ramps output to zero over 5 ms while timeline/state advance, then freezes in paused. Resume continues frozen state with a 5 ms fade-in. Stop suppresses new admissions and fades existing sound for 5 ms, then clears voices and returns ready at frame zero. Stop from paused is immediate because output is already silent. Panic immediately clears sound and returns ready at zero (emergency hard mute, may click). Seek is accepted only while ready/paused/finished; it clears tails and starts fresh at the requested frame without reconstructing earlier notes. Stop, panic and seek advance a generation; stale commands are rejected. Loop, live audition, device loss and concurrent plan swaps are explicitly unsupported until the native device milestone.

## Render/file boundary

Write an offline stereo 24-bit WAVE_FORMAT_EXTENSIBLE PCM file with explicit little-endian chunks, fact sample count, front-left/front-right mask and embedded LIST/INFO/ICMT JSON recipe. Quantisation rounds to the nearest integer without dither in this first reference export; exact silence is retained. The embedded recipe records verified session content hash/integrity, input count/truncation, mode/speed, engine/recipe revision, sound settings and admission totals. This is traceability, not a complete portable musical session (M3/M5).

Render in fixed scratch storage; validate duration/RIFF capacity and available space before writing. Stage in an exclusively created sibling directory, then publish the completed WAV with an exclusive hard link; never overwrite an existing output, library or symlink. Unsupported hard-link filesystems fail explicitly. RAII removes only this render's staging directory on ordinary failure; an interrupted process may leave a `.rendering` directory for inspection. No automatic recovery deletes someone else's files.

## Evidence gates

Before integration: independent Python equation oracle and WAV parser; exact admission/frame fixtures; sparse and overload partition comparisons; silence/empty windows; every rate extreme and parameter extreme; invalid settings; release/steal/stop/pause/panic/seek/generation; allocation/deallocation counter around process/controls. Tests remain enabled in Release. Debug/Release and ASan+UBSan must pass.

After integration: render a disposable synthetic corpus, verify hashes/headers/recipe and no-overwrite behaviour, reject corrupt source material before publishing, compare PCM at different block sizes, check peak/RMS/DC and oracle error (≤2e-7 full scale including float/24-bit rounding). Measure scalar Release process timing on named hardware at multiple block sizes; report percentiles and outliers as synthetic measurements, not DAW/GUI/device qualification. Expose clips for owner listening. No claim of subjective quality or completion of listening acceptance without that review. Numerical tests cannot decide the product's musical identity.

## Sources and provenance

[Julius O. Smith, additive synthesis](https://www.dsprelated.com/freebooks/sasp/Additive_Synthesis.html) supplies the established sinusoidal-envelope principle. Equations, mapping, limits, coefficients and implementation here are project choices independently specified above. [Microsoft WAVEFORMATEXTENSIBLE](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible) defines format fields/channel masks; [IBM/Microsoft RIFF specification](https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/Docs/riffmci.pdf) defines chunks and INFO metadata. The Field Audio DSP skill/reference informed proof obligations. No source code or proprietary tuning is imported; no paid dependency is added.

## Owner clarification and applicable V2 methods

The owner selected Field DSP V2, then explicitly clarified that these effect-oriented skills must not limit this whole synthesizer's research or design scope. V2 supersedes V1 only where their relevant engineering/evaluation guidance overlaps. No Field/Eva hardware, six-string or universal effect-chain assumptions apply. The complete palette, voice relationships, modulation, species/individual mappings, regional effects and historical composition remain product targets. Eight primary voices and this tone are first-slice laboratory settings, not product ceilings.

The simplest neutral comparison is one sine oscillator under the same envelope and timing. The three-partial tone is a hypothesis that light upper harmonics improve onset/pitch legibility without becoming harsh. No comparative listening has established that benefit; the sine baseline remains a valid final choice. Only one candidate is active. A future richer palette must be chosen for musical tasks, not to satisfy a template quota. Generate fixed-gain RMS-matched, concealed-label sparse and dense pairs, retain original levels, and ask the owner about legibility, masking, harshness and useful decay. Synthetic development examples are not held-out wildlife or listening evidence.

The 5 ms cosine transitions are an explicit initial setting for this short struck voice and its finite retirement-pool proof, not a universal smoothing recommendation. Tests must check normal-level stop/resume/repeated retarget as well as extremes; listening remains pending. Stable revision-1 CLI/recipe IDs are `root_midi` (integer MIDI), `attack_seconds`, `decay_seconds`, `gain_db` and `sample_rate`. They are immutable per plan; recall reconstructs a plan; no moving automation or undo is offered by this first renderer. 0 dB is unity master, -60 dB is attenuation rather than exact mute; pause/stop/panic provide exact silence. Sound input loss is irrelevant to a frozen no-network render. There is no dry/wet split or effects bypass because this is a generator.

Research reading depth: Smith's additive-synthesis section (selected full section), Microsoft WAVEFORMATEXTENSIBLE fields/remarks, and the RIFF chunk/INFO format sections were consulted on 7 November 2026. Published principles define sinusoidal envelopes and file formats; the particular three partials, mapping and voice limits are independently authored hypotheses. No implementation source was imported. Relevant ownership/evidence checks from Field Audio Systems inform the native boundary; hardware/callback and subjective claims remain separate.
