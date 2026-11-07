# Standalone beta quick start

The graphical instrument is **Releases/Live Sonification/Animal Atlas.app**. It is distinct from the older root-level Animal Atlas.app that opens the Python collection menu. This local beta supports Apple Silicon on macOS 15 or later. It includes its interface, fonts, map assets, native engine and SQLite; running it needs no Terminal, Node, Vite, hosted service or subscription.

## Hear new detections

In **Atlas → Live**, choose **Collect → Start live updates**. Apply your stereo output in **Audio settings**, then enable **Live listening**. Each eligible new observation plays one synthesized note through its species voice as the globe updates. The first poll establishes a baseline. Leave listening on for subsequent provider polls. Open **Live listening status** for counts of received, admitted and dropped detections and skipped revisions.

Edit a species in **Sound Studio** to hear the draft on its next detection. An unedited species uses its saved settings or exact-species preset when available. Other species use a neutral timbre with a species-derived pitch. Live uses the direct voice. Replay adds the composition and geographic effects for prepared pieces.

A busy poll can yield more observations than the instrument can play. The worker examines up to 512 recent revisions and submits up to eight notes per pass. Turning listening off, pausing collection, changing library or output, or entering Replay stops the live performance. Returning to Live requires enabling listening again. After a service interruption longer than five seconds, listening resumes at the current end of the same library.

## Play a saved soundscape

1. Open the graphical app. It starts silently in scene-only mode.
2. Use **File / Project → Import soundscape** for a verified `.atlas-scene`, then open **Project**, select it under **Soundscapes** and choose **Open soundscape**. The synthetic sparse/dense audition scenes under `Auditions/regional-soundscapes` are engineering examples, not wildlife observations. Alternatively, **File / Project → Choose wildlife library** attaches an explicitly selected existing database read-only, including the project's `.state/atlas.sqlite3` if you want your collected material. External libraries remain read-only. The separate native Collect control is described below.
3. Open **Audio settings**, choose system default or an explicit output and buffer, then **Apply output**. Applying stops playback. A device must support the saved piece's sample rate and stereo output.
4. Enable **Listening**, then press **Play**. Neither app launch, scene recall nor device recovery starts sound automatically. The current master level is fixed at -18 dB.
5. Edit voices/composition/regions in Sound Studio. In **Project**, **Save soundscape** creates an immutable complete recipe. **Export saved WAV** renders that saved recipe; its download link opens the native Save dialog. Choose a new destination: existing files are not overwritten.

If an output disconnects, refresh the devices, apply an available choice, then explicitly enable Listening and Play again. A missing explicit device is retained in preferences and never silently replaced. Device identities use JUCE's type and name; renamed hardware needs reselection.

## Save a wildlife piece

- To freeze previously collected observations offline, choose **File / Project → Open collected wildlife library**, then **Project → Saved pieces → Freeze collected interval**. Enter a name and UTC start and end. Optionally choose a source or species. The end time is exclusive. A quiet window can be saved with zero records. It records what the collector had stored when you froze it. Wildlife coverage remains unknown.
- To import, choose **File / Project → Open historical spotted seal library** or attach your own compatible wildlife library. In **Replay**, select a saved piece. Choose **Project → Import selected piece to managed library**. To resume an interrupted import, attach the same library, select the same piece and import again. The app verifies the source again and reuses the latest matching checkpoint. Starting a different import replaces that checkpoint.
- Choose **Open saved piece** when the job finishes. Replay preserves the observed timestamps and gaps. For the bundled five-fix excerpt, choose **240×** to hear its two days of observations within the preview limit. For the full seal deployment, choose **Fit full interval**. **Arrival** follows when the records entered the source library. Enable **Listening** and press **Play** to hear the result.
- Use **Sound Studio** to edit voices, then **Project → Save soundscape → Export saved WAV**. Each saved soundscape contains its events and settings and can be included in a project backup. The project backup excludes the managed wildlife library itself.

A freeze accepts up to 366 days, 50,000 events and 64 MiB of event payload. Capacity errors leave the existing library intact. For observation windows too long for the 30-minute preview at maximum speed, shorten the interval.

## Collect current wildlife activity

Close the previous graphical app before opening this build; only one app can use the same app-storage directory. Choose **Collect**, choose worldwide or NZ-only bird coverage, NZ shark positions and optional worldwide reported sightings, then **Start live updates**. The app opens its managed live library and refreshes the interface. It needs an internet connection but no terminal process, Python runtime, account, paid key or subscription. Listening is separate: collection itself produces no audio.

BirdWeather reports species detections at acoustic receiver stations, checked no more often than once a minute. NZSharks reports latest public source-subject positions, checked no more often than every 15 minutes. Shark observation times can be hours or days old even after a successful fetch. These are not continuous identified-animal tracks. The source rows show last fetch and actual observation age separately; expand details for coverage, gaps and retry information. The collector requests up to four pages of 250 bird detections per poll. Busy windows may exceed that limit: records still arrive, genuinely fresh later detections may ping, and partial coverage remains explicit. Initial/restarted backlogs, corrections and older backfills do not ping.

**Worldwide birds** rotates through four listening areas each minute, returning to each of eight areas about every two minutes. The first visit to each area establishes a baseline. Pings can begin on the next visit when a newer observation arrives. Busy areas can fill their 250-record page. **NZ birds** spends the same budget on up to four NZ pages. Both modes deduplicate by BirdWeather detection ID. Switching modes respects provider cooldowns. Open the worldwide source’s **Collection details** for regional checks. Geographic bounds represent sampling areas and overlap in places.

**Worldwide reported sightings** samples up to 30 recent public iNaturalist Animalia reports per 15-minute cycle, using exact observation times in the last 24 hours and updated-record ordering. It admits CC0 and CC BY observations with publicly open locations and keeps observer attribution and reported accuracy. An additional bounded check revisits existing observations for changes and withdrawal. Time spent between rechecks grows with library size. Frozen exports are independent copies. Open **Map details** to filter receivers, animal fixes and reported sightings. A triangle marks a reported sighting.

For a historical individual, choose **File / Project → Open historical spotted seal library**. The app includes the archive and its attribution and opens it offline. In Replay choose the final-five-fixes excerpt or the complete deployment. The archive contains the actual dated positions and uncertainties of one tagged seal. Selecting an external library pauses live collection. [Archive and attribution](../deliverables/ATN-spotted-seal-2018/README.md)

In **Atlas → Live**, **Latest detections** lists up to 40 loaded observations by observation time. When the list fills, it reserves some places for quieter sources and regions. The map outlines the eight newest observations with available locations among those eight. A larger ring marks the newest mapped observation when the first entry has a position. Choose a row to inspect it and face its location on the globe. A New Zealand selection also moves the circular detail map. Incoming records preserve your chosen view. The full observation date and time remain visible for older reports.

Use **Collect → Pause updates** to stop. Collection runs only while this app is open and starts paused after each launch. Starting again preserves existing collected records; provider intervals and Retry-After still apply. Choosing a different wildlife library pauses the native collector. No owner-started external collector is altered. Imported soundscapes, existing libraries and voice settings remain separate.

The Replay saved-piece browser has Previous, Next and Refresh controls, with 50 pieces per page. It preserves selection across screen changes, accepts thousands of saved pieces and asks for refresh when a library changes during browsing. It shows interrupted/recording pieces without treating them as frozen. Material preparation reuses one verified piece until the library revision changes; an external mutation invalidates it. Use **Project → Saved pieces → Freeze collected interval** to prepare a Replay piece from the managed library.

## Your files

The default writable folder is `~/Library/Application Support/Animal Atlas`. **File / Project → Show app storage** reveals it. It contains `voices.sqlite3`, `live.sqlite3`, `soundscapes/`, `exports/` and `settings.json`. The managed live database is capped at 512 MiB; insufficient storage is reported rather than silently deleting records. The app never uses its bundle as writable storage. Import preserves the source file and verifies the complete recipe before publishing a new scene copy. Invalid settings are retained and reported, not silently overwritten. Only one app may write a given state directory.

Scenes are self-contained for recall/render. Attaching a library does not copy it into app storage or alter it. A previously chosen library is remembered, but collection remains paused after launch. Source availability, permission and collection remain separate from generating audio from saved material. Importing a scene refreshes the interface and turns Listening off. Running exports retain their progress and Cancel control after an interface reload. Open **Project → Saved audio exports** to find completed WAVs after restarting; **Save WAV** copies to a new destination and **Show file** reveals the managed file. Interrupted staging and damaged WAVs are reported separately and never appear as completed audio.

## Move or back up a project

**Project → Export scene** copies the selected sealed `.atlas-scene` to a new file, including its events, captured voices, composition and effects. It preserves the exact saved bytes.

**File / Project → Back up project** lets you select scenes and saves a single `.atlas-project` containing those scenes plus independently saved voices. Save voice drafts or capture a soundscape first if you want to retain current edits. The archive excludes wildlife libraries (including `live.sqlite3`), unsaved drafts, audio-device settings and rendered WAVs; save WAVs separately from the export history.

**File / Project → Restore project into a new folder** verifies the archive, previews counts/names and asks for a new folder name. Existing folders are never replaced, including empty folders. A successful restore opens a separate app window using that folder; the current project stays open. Scenes can be recalled and rendered without the original wildlife library. Project file operations pause listening; explicitly resume when ready. **Storage and library status** identifies active library/scene-only mode and app storage independently of the most recent action.

## Navigate New Zealand

Drag inside the small NZ circle to move around the country at the original detail scale. Markers move with the map and remain selectable. The circle and main globe stay fixed. Use arrow keys while the detail map is focused (Shift for larger steps), Home or **Reset view** to recenter. The view persists when switching between Atlas and Sound Studio.

## Build from existing local dependencies

This repository does not automatically fetch JUCE or paid services. Use an existing JUCE9 checkout, compatible SQLite, CMake, Python3 and the frontend build toolchain. On the current development Mac:

```sh
cd '/Users/henryjones/Documents/ChatGPT/Generative Nature Synth'
(cd ui/app && npm run build)
cmake -S . -B build/standalone \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DATLAS_BUILD_STANDALONE=ON -DATLAS_BUILD_AUDIO_HOST=ON \
  '-DATLAS_JUCE_SOURCE_DIR=/Users/henryjones/Documents/Audio/Project-Ceramic-Kiln/third_party/JUCE' \
  -DSQLite3_INCLUDE_DIR=/opt/homebrew/opt/sqlite/include \
  -DSQLite3_LIBRARY=/opt/homebrew/opt/sqlite/lib/libsqlite3.dylib
cmake --build build/standalone -j4
ctest --test-dir build/standalone --output-on-failure
cmake --build build/standalone --target atlas-standalone-package
python3 -B tests/standalone_smoke.py \
  --binary 'build/standalone/Animal Atlas.app/Contents/MacOS/Animal Atlas' \
  --seconds 3 --evidence build/standalone/smoke.json
```

The default smoke is offline. Add `--live` explicitly to contact both public sources in disposable state, exercise the actual Collect control/reload, wait for a second bird poll and verify new pings plus rendered map/species data. It requires current provider availability and takes roughly 70 seconds.

The smoke launches the real bundled WebKit interface, discovers devices and exercises native requests in disposable state. It does not enable physical output. Add `--scene /absolute/path/example.atlas-scene` to verify import, recall, native offline rendering, intercepted scene/WAV saves and verified backup/restore in disposable state. The development frontend tests currently use Node22 with `--experimental-transform-types`; this is not a runtime dependency.

`scripts/package_standalone.py` bundles SQLite and notices, checks dependencies, signs locally and verifies the seal. It can additionally copy the app to a **new** `--copy-to` path and create a **new** `--zip` archive; it refuses existing destinations. Its package manifest records exact UI/dependency hashes. Rebuilding the generated package is supported and leaves app data untouched.

## Evidence and remaining qualification

See the [live sonification qualification](../evidence/live-sonification/qualification.md), [worldwide qualification](../evidence/worldwide/README.md), [latest detections qualification](../evidence/latest-detections/qualification.md), [live-library qualification](../evidence/library-live/qualification.md), [source evidence and limitations](data/LIVE_SOURCE_MILESTONE.md), [project recovery evidence](../evidence/project-recovery/qualification.md), earlier [qualification](../evidence/standalone/qualification.md), [milestone plan](milestones/STANDALONE_PLAN.md), [shell contract](engineering/standalone-shell.md) and [device UI notes](../ui/app/docs/NATIVE_AUDIO_DEVICES.md). Local ad-hoc signing is an integrity check, not Developer ID signing or notarisation. This local bundle is not a public binary release. Human listening, physical hotplug/sleep/wake testing, another-machine qualification and VST3 hosting remain separate work. Field live-input latency budgets do not apply to this generative instrument.
