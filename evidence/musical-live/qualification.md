# Musical Live · 0.8.0

The Apple Silicon standalone and a separately extracted ZIP passed strict code-signature verification. Each ran the production React interface, native collector and synthesizer with disposable state and ZoomAudioDevice virtual stereo output. The [release record](release.json) includes package and audio hashes, output peaks and bounded playback counts. The [standalone guide](../../docs/STANDALONE.md#hear-new-detections) covers the controls.

In each packaged run, one sparse observation sounded immediately on the next service pass. Its map entry showed the struck species voice and geographic sector. One 500-record poll then produced four sounded representatives, 244 grouped observations and 252 skipped for playback at density four. Duplicate and corrected source records produced no additional notes. The meter showed rendered output and cleared on mute. These fixture counts describe musical selection, not wildlife coverage.

The native fixture additionally exercised authored timbres, five geographic sectors, stereo region effects, settings edits while armed and homogeneous rhythmic bursts. A mid-phrase stop left the output silent. A corrected representative and a withdrawn representative were suppressed before their scheduled onsets. The historical seal regression imported and saved five original events and exported nonzero stereo PCM with the historical source hash intact.

All 40 Release CTest suites, 78 frontend tests and five focused AddressSanitizer/UndefinedBehaviorSanitizer suites passed. Headless Chromium exercised both screens at 1440 × 900 and 1280 × 800, including saving edits, draft retention, outcome labels, metering and bounded polling. Hardware listening preference, whole-product long-session performance and DAW hosting remain separate qualifications.

## Listening comparison

The two examples use synthetic wildlife records. The developer fixture captures the native stereo callback at 48 kHz. The files under `Auditions/musical-live/listening/` use an identical +30 dB monitoring gain applied before PCM16 export. Raw captures are in the parent folder. Both listening copies played through the selected MacBook Pro Speakers using the existing system volume. No microphone recording or human species-recognition score was collected.

To regenerate the local examples from a built checkout:

```sh
build/standalone/atlas-musical-live-tests --auditions Auditions/musical-live
```

To repeat the packaged test with ZoomAudioDevice available:

```sh
python3 -B tests/live_sonification_package.py \
  --binary 'Releases/Musical Live/Animal Atlas.app/Contents/MacOS/Animal Atlas' \
  --fixture build/standalone/atlas-live-collection-fixture
```
