# Smoothed live playback qualification

Animal Atlas 0.9.0 adds a third live listening mode on Apple Silicon macOS. Smoothed retains one note per eligible detection across poll and queue boundaries. Production scheduling, SQLite admission and audio playback run in C++.

## Checked behavior

- A 600-detection native fixture produced 600 confirmed starts and 600 individual sounded outcomes. A correction during a second burst retired the affected pending note. Stopping and rearming did not replay the backlog.
- FIFO admission covered 1,200 eligible records over multiple pages. Timing fixtures covered irregular ascending and descending observation times, repeated timestamps, empty polls and regional BirdWeather collection. Reopening a database with the earlier indexes preserved the store identity and revision count.
- The signed local app and a separately extracted ZIP each received one isolated detection followed by 500 detections from eight species. Each reported 501 callback-confirmed starts, zero grouped, zero dropped, zero skipped revisions and zero pending after draining. The app showed pending work during the burst. The packaged test selected real fixture rows on the map, inspected outcomes, observed a non-silent output meter and verified that mute cleared it.
- The same release passed the earlier Musical mode fixture. The 41 native release tests, seven targeted address/undefined-behavior sanitizer suites and 79 frontend tests passed.

## Reproduce

Run the commands in the [standalone build guide](../../docs/STANDALONE.md). From the repository root, exercise the packaged app with the existing separate virtual audio output:

```sh
python3 -B tests/live_sonification_package.py \
  --binary 'Releases/Smoothed Live/Animal Atlas.app/Contents/MacOS/Animal Atlas' \
  --fixture build/standalone/atlas-live-collection-fixture --mode smoothed
```

The qualification uses an isolated collection and virtual stereo output. Sustained input above playback capacity adds delay. This run did not measure long-term provider throughput or physical listening quality. The macOS build is locally ad-hoc signed.

[Package hashes and measured counters](release.json)
