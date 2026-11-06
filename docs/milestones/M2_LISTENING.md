# M2 synthetic listening comparison

These are development excerpts, not wildlife recordings, held-out evaluation, listening acceptance or hardware qualification. The first three-partial voice remains a hypothesis. The full synthesizer palette, mappings, modulation, regional effects and composition targets remain open.

Generate a new directory from the canonical repository:

```sh
python3 -B scripts/m2_listening_corpus.py --binary build/native/atlas-native --output build/m2-listening
```

The script refuses an existing output directory, uses temporary SQLite only, and makes no provider requests. Sparse and dense excerpts use the same admitted onsets, root MIDI 60, 48 kHz, 8 ms attack, 650 ms decay, 20 ms terminal fade, 0 dB master and speed 1. Dense material has eight overlapping notes without stealing, so the independent neutral sine and native three-partial candidate share timing and envelopes. Each excerpt includes native and sine originals plus concealed A/B files. The neutral sine is test-only, not a second production engine.

Whole-excerpt RMS includes gaps. One fixed gain per condition attenuates the louder original to the quieter original; there is no AGC, envelope flattening or boost. The manifest records seed 20261107, label reveal, gains, RMS, peaks, hashes, source recipe and native render evidence. Reproduction with the same build produces identical WAVs. Avoid opening `manifest-reveal.json` or the descriptively named originals until after comparing A/B.

Listen at a comfortable fixed playback level. Alternate sparse A/B, then dense A/B; repeat once if useful. Before revealing labels, record which makes onset and pitch easier to follow, whether upper harmonics become harsh, whether dense notes mask each other, and whether decay supports gaps. Record preference separately from ability to detect a difference. Record playback device/headphones, playback level, order, fatigue and comments; no automatic pass threshold is claimed. Owner selection and adverse examples determine the next comparison. Listening and hardware status remain `not_run` until evidence is recorded.

## Saved-session launcher

Run `Audition Atlas Session.command` deliberately to render the latest saved library session read-only. Defaults: `.state/atlas.sqlite3`, observed time at speed 60, 48 kHz, MIDI root 60, 8 ms attack, 650 ms decay and 0 dB master. `ATLAS_LIBRARY`, `ATLAS_BINARY` and `ATLAS_SPEED` may override their stated defaults. Each invocation writes a unique ignored `build/auditions` directory containing WAV and JSON evidence. Rendering does not start playback: enter `y` at the explicit prompt to use macOS `afplay`. Control-C stops that player; collector processes are untouched. This is offline audition, with no device callback, transport UI or live sound control claim.
