# Standalone beta quick start

The graphical instrument is **Releases/Standalone Beta/Animal Atlas.app**. It is distinct from the older root-level Animal Atlas.app that opens the Python collection menu. This local beta supports Apple Silicon on macOS 15 or later. It includes its interface, fonts, map assets, native engine and SQLite; running it needs no Terminal, Node, Vite, hosted service or subscription.

## Open and play

1. Open the graphical app. It starts silently in scene-only mode.
2. Use **Import soundscape** for a verified `.atlas-scene`, then select it under **Soundscapes** and choose **Open soundscape**. The synthetic sparse/dense audition scenes under `Auditions/regional-soundscapes` are engineering examples, not wildlife observations. Alternatively, **Choose wildlife library** attaches an explicitly selected existing database read-only, including the project's `.state/atlas.sqlite3` if you want your collected material. The app does not start or alter collectors.
3. Open **Audio settings**, choose system default or an explicit output and buffer, then **Apply output**. Applying stops playback. A device must support the saved piece's sample rate and stereo output.
4. Enable **Listening**, then press **Play**. Neither app launch, scene recall nor device recovery starts sound automatically. The current master level is fixed at -18 dB.
5. Edit voices/composition/regions in Sound Studio. **Save soundscape** creates an immutable complete recipe. **Export saved WAV** renders that saved recipe; its download link opens the native Save dialog. Choose a new destination: existing files are not overwritten.

If an output disconnects, refresh the devices, apply an available choice, then explicitly enable Listening and Play again. A missing explicit device is retained in preferences and never silently replaced. Device identities use JUCE's type and name; renamed hardware needs reselection.

## Your files

The default writable folder is `~/Library/Application Support/Animal Atlas`. **Show app data** reveals it. It contains `voices.sqlite3`, `soundscapes/`, `exports/` and `settings.json`. The app never uses its bundle as writable storage. Import preserves the source file and verifies the complete recipe before publishing a new scene copy. Invalid settings are retained and reported, not silently overwritten. Only one app may write a given state directory.

Scenes are self-contained for recall/render. Attaching a library does not copy it into app storage or alter it. No library is attached automatically. Source availability, permission and collection remain separate from generating audio from saved material. Importing a scene refreshes the interface and turns Listening off. An existing background export may finish in `exports/`, although its selected job panel resets; persistent export history is future work.

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

The smoke launches the real bundled WebKit interface, discovers devices and exercises native requests in disposable state. It does not enable physical output. Add `--scene /absolute/path/example.atlas-scene` to verify import, recall, native offline rendering and the intercepted WAV save link. The development frontend tests currently use Node22 with `--experimental-transform-types`; this is not a runtime dependency.

`scripts/package_standalone.py` bundles SQLite and notices, checks dependencies, signs locally and verifies the seal. It can additionally copy the app to a **new** `--copy-to` path and create a **new** `--zip` archive; it refuses existing destinations. Its package manifest records exact UI/dependency hashes. Rebuilding the generated package is supported and leaves app data untouched.

## Evidence and remaining qualification

See [qualification](../evidence/standalone/qualification.md), [milestone plan](milestones/STANDALONE_PLAN.md), [shell contract](engineering/standalone-shell.md) and [device UI notes](../ui/app/docs/NATIVE_AUDIO_DEVICES.md). Local ad-hoc signing is an integrity check, not Developer ID signing or notarisation. This local bundle is not a public binary release. Human listening, physical hotplug/sleep/wake testing, another-machine qualification and VST3 hosting remain separate work. Field live-input latency budgets do not apply to this generative instrument.
