# Retrospective 24-hour worldwide observation qualification

**Fixed completed observation window:** 6 November 2026 04:00:00 UTC inclusive to 7 November 2026 04:00:00 UTC exclusive. Retrieval: 7 November 04:52:24–04:53:19 UTC. This demonstrates actual observations in a retrospective 24-hour window; it does **not** demonstrate 24-hour continuous collector uptime.

The checked aggregate receipt records **236 unique accepted observations from two independent providers across six coarse geographic areas**, including non-bird taxa. iNaturalist contributed 235 public, unobscured CC0/CC-BY Animalia reported observations; the Sustainable Oceans Society public NZSharks endpoint contributed one latest satellite-position snapshot whose actual observation time falls inside the same window. Sharks are provider subjects, not newly verified individual/deployment identities. This qualification is separate from the delivered identified ATN seal history archive.

| Coarse geometric area | iNaturalist | NZSharks | Total |
|---|---:|---:|---:|
| Oceania/Pacific | 87 | 1 | 88 |
| Africa area | 12 | 0 | 12 |
| Europe area | 36 | 0 | 36 |
| Asia area | 48 | 0 | 48 |
| North America area | 34 | 0 | 34 |
| South America area | 18 | 0 | 18 |

Taxon sample: 98 Insecta, 76 Aves, 16 Arachnida, 12 Mollusca, 11 Mammalia, 11 unspecified Animalia, five Reptilia, four Amphibia, two Actinopterygii, one Chondrichthyes. There are 214 distinct supplied scientific names; these are not 214 independently taxonomically verified species.

All 24 hourly bins yielded accepted iNaturalist samples (8–10 each). Every query hit its 10-record sample cap: this is partial geographic/taxon availability evidence, not completeness, abundance or a statistically representative world survey. Of 240 returned sightings, the exact production normalizer admitted all 240; five of those fell outside the strict requested hourly interval and were excluded, leaving 235. Only returned actual observation timestamps determine membership; query success alone does not establish time coverage. The second provider returned 28 normalized snapshots: 27 observations fell outside the window and only one qualified. Historical hourly recovery is unavailable from that snapshot endpoint; this does not imply sharks reported in all 24 hours. Neither source proves each region has observations in every hour, or that an unobserved hour/area was ecologically quiet.

Observation age at qualification start: minimum 0.876 h, median 11.940 h, p95 22.979 h, maximum 24.098 h. These are observed ages, not upload delays. Source publication times, continuous provider reporting cadence, future availability and uptime remain unqualified. Broad rectangular region assignments are disjoint and computed directly from normalized coordinates; approximate names do not claim precise continental or political boundaries.

Requests were sequential, at least two seconds between starts, 20-second timeouts, 4 MiB maximum responses and no retries. The run made 27 requests: 24 one-page hourly iNaturalist queries (first ten by descending observation time), then three public shark bootstrap/config/subject requests. The earlier candidate-parser run and single pilot query are outside this final run’s 27-request receipt. No owner database or process was touched. Temporary provider responses and disposable normalization database were removed; no raw sightings, observer identities, subject identities, precise positions or private credentials are retained in this receipt. URLs and SHA-256 response hashes support provenance without publishing raw sightings. Licence admission for iNaturalist concerns observation data; media were neither downloaded nor granted reuse rights. Shark reuse remains unresolved independently of existing public technical access; this aggregate access qualification does not resolve commercial redistribution rights.

## Reproduce

Use the completed final release libraries, then link the small developer probe against them (no rebuild needed):

```sh
clang++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -I native/include evidence/worldwide/normalizer_probe.cpp build/standalone/libatlas_storage.a build/standalone/libatlas_playback.a build/standalone/libatlas_audio.a build/standalone/libatlas_core.a -lsqlite3 -o /tmp/atlas-world-normalizer
python3 -B evidence/worldwide/qualify_24h.py --normalizer /tmp/atlas-world-normalizer --end 2026-11-07T04:00:00Z --output /tmp/worldwide-qualification.json
```

This final receipt supersedes the earlier candidate-parser run. It uses committed production source revision `0d76b0822370c35990c6109ee573ab7e51c51d91`; the three production normalizer source hashes were checked directly against Git objects at that revision. Hashes of all four linked final `build/standalone` static libraries and the probe executable are recorded. The query explicitly requires both `geoprivacy=open` and `taxon_geoprivacy=open`; the final parser independently validates privacy types and UTC timestamps.

The probe calls the exact `parseInaturalistResponse` and production `Collector::collect("nzsharks")`; it does not duplicate normalization. Script and helper are development qualification tools, not required user runtimes. Native source and binary hashes are in the receipt. Public sources can change after retrieval, so repeat aggregate counts may differ; this is a point-in-time receipt, not a frozen raw dataset.

Primary provenance: [iNaturalist API](https://api.inaturalist.org/v1/docs/), [API usage guidance](https://www.inaturalist.org/pages/api+recommended+practices), [privacy guidance](https://help.inaturalist.org/en/support/solutions/articles/151000169938), [Sustainable Oceans Society conservation project](https://www.sustainableoceansociety.co.nz/projects/shark-conservation). Exact public request URLs, byte counts, retrieval times and SHA-256 digests are recorded in `retrospective-2026-11-07.json`.


## Standalone 0.5.0

The installed Apple Silicon app completed two worldwide bird sweeps in a disposable project. It issued the real public requests, returned eight regional health entries and displayed 372 locations on the globe. The worker retained 1,385 BirdWeather detections, 30 iNaturalist reports and 28 NZ shark snapshots. The initial 500-record map included all three providers. Baselines produced zero live pings. Capped pages and rejected future or invalid records were identified in the source details. [Packaged app verification](standalone-release.json)

In the existing user's library the previous catalogue sampled 2,000 events out of 2,604. The complete identity lookup found 26 species and 46 receivers, recovering two of each. The UI now shows a catalogue limit only if distinct metadata exceeds a display budget. At high event volume, the map and feed reserve space for quieter sources and geographic sectors. The native, UI, sanitizer and browser regressions cover this behavior.

The bundled identified-animal archive is available offline under **File / Project → Open historical spotted seal library**. The archive contains two saved pieces and includes the source acknowledgements. The separate saved-scene smoke verified native 48 kHz stereo WAV export and project restore. [Archive and attribution](../../deliverables/ATN-spotted-seal-2018/README.md)

## Normally paced live arrival revisit

`worldwide-arrivals.json` records three real `CollectionService` worldwide sweeps in a new disposable library, with only `birdweather_world` enabled and default production pacing. The first two geographic sweeps admitted 1,389 baseline records with zero live-eligible revisions. The third sweep revisited the first four areas and admitted 613 additional records: 582 genuine live-eligible arrivals and 31 older baseline records. Final totals were 2,002 unique records and 582 live-eligible revisions over 130.022 seconds. All eight slots were polled; seven returned observations. Eight of twelve requests hit their explicit one-page cap; no transport/schema errors, invalid records or future deferrals occurred. This verifies acquisition eligibility, not playback or rendered UI pulses.

`worldwide_arrivals.cpp` runs the actual service with its default network adapter and scheduler, prints aggregate snapshots after each completed sweep, disables collection, and removes its temporary database. It times out after 240 seconds. Build it against the existing standalone libraries and JUCE core objects (no production rebuild required). On this checkout:

```sh
ROOT="/Users/henryjones/Documents/ChatGPT/Generative Nature Synth"
JUCE="/Users/henryjones/Documents/Audio/Project-Ceramic-Kiln/third_party/JUCE"
OBJ="$ROOT/build/standalone/CMakeFiles/atlas-standalone-support-tests.dir${JUCE}/modules/juce_core"
clang++ -std=c++20 -O2 -DNDEBUG -mmacosx-version-min=15.0 \
  -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_USE_CURL=0 -DJUCE_STANDALONE_APPLICATION=1 \
  -I "$ROOT/native/include" -I "$ROOT/native/src" -I "$JUCE/modules" \
  "$ROOT/evidence/worldwide/worldwide_arrivals.cpp" "$ROOT/native/src/juce_collection.cpp" \
  "$ROOT/build/standalone/libatlas_storage.a" "$ROOT/build/standalone/libatlas_core.a" \
  "$OBJ/juce_core.mm.o" "$OBJ/juce_core_CompilationTime.cpp.o" "$OBJ/juce_core_zlib.c.o" \
  /opt/homebrew/opt/sqlite/lib/libsqlite3.dylib \
  -framework Cocoa -framework Foundation -framework IOKit -framework Security \
  -o /tmp/atlas-world-arrivals
/tmp/atlas-world-arrivals
rm /tmp/atlas-world-arrivals
```

The receipt retains only aggregate counts, coarse region identifiers and timestamps; no species/observer/subject identities or precise locations. Repeat counts vary with real provider activity. Required code/library and driver hashes bind this receipt to its observed run.
