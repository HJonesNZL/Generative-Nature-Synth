# Live detection sonification · 0.7.0

The installed Apple Silicon app and a separately extracted copy of its release ZIP passed the packaged WebKit test. Both signed bundles passed strict signature verification and contained identical executable bytes. The [release record](release.json) includes SHA-256 digests and per-run device results.

A disposable native collector established a baseline, then ingested an injected BirdWeather observation after listening was armed. The latest-detection map control displayed that observation and the host admitted one note. A repeated provider record, a correction and a backfill produced zero additional notes. Both runs received output callbacks on the explicitly selected ZoomAudioDevice virtual device, then muted and shut down. This exercise used synthetic provider responses and disposable state. Physical listening and production provider availability were outside the test.

All 38 Release CTest suites and 69 UI tests passed. Three dedicated live suites passed with AddressSanitizer and UndefinedBehaviorSanitizer. Native output-buffer tests covered edited species timbres, silence before an onset, bounded bursts, panic and recovery. The host continued to sound incoming species after the editable catalogue filled.

The packaged historical regression prepared the complete 224-fix seal deployment. It imported the five-fix excerpt to disposable managed storage, saved its soundscape after reload, and exported a 899,254-byte WAV with nonzero PCM. The source library retained its original hash.

To repeat live qualification, build the standalone and the atlas-live-collection-fixture target, then run the following with the separate virtual device available.

```sh
python3 -B tests/live_sonification_package.py \
  --binary 'Releases/Live Sonification/Animal Atlas.app/Contents/MacOS/Animal Atlas' \
  --fixture build/standalone/atlas-live-collection-fixture
```
