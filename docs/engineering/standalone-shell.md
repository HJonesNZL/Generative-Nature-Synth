# Packaged macOS shell contract

Animal Atlas.app hosts the production React build in JUCE 9.0.0's WKWebView. The bundle contains `Contents/Resources/ui`; running it requires neither Node, Vite, a web server, a CDN nor a configured wildlife database. Node is only a build/test dependency. This is a local personal engineering build, not a signed/notarised distribution claim.

## Ownership and bounds

The JUCE message thread owns the window, native library/import/save choosers and webview. One serial worker owns `PlaybackRuntime`, `VoiceStore` and `UiHost`, their database/file work, state persistence, and import/export operations. JUCE's output adapter continues to arrange device lifecycle on the message thread. The existing playback renderer and its callback contract are unchanged by the shell. The application is a generative playback and offline rendering instrument; this milestone does not impose live-input or low-latency requirements.

The worker preloads at most 512 regular bundled files, 16 MiB per file, 64 MiB total before constructing the browser. Symlinks are rejected. The resource provider then performs only exact map lookups and returns local MIME-labelled bytes. Main-frame navigation is restricted to the JUCE resource origin; popups do not navigate. No external origin is enabled for native integration.

The document-start script injects the existing `window.atlasHost.request` interface. Two JUCE events carry request/reply envelopes; no `NativeFunctionCompletion` referencing a destroyed JUCE provider is retained. There are at most 16 frontend pending requests, 16 queued worker operations, a 16 KiB UTF-8 request maximum, an 8 MiB reply maximum, and a 30 second transport timeout. Abort/timeout/page exit retire pending frontend entries. An admitted edit may complete after its caller aborts; the host never automatically retries mutations. The existing UI polls native state; `subscribe` returns a no-op unsubscribe function.

The standalone serial worker renews the playback heartbeat on each loop, including its 100 ms idle wakeups. Playback therefore does not depend on visible React polling when the window is hidden, minimised or background browser timers are throttled. Each loop services existing faults and an expired worker lease before renewal; an actual stalled worker or stopped application still has the runtime's existing lease protection. Shutdown stops renewal and retires the runtime as described below. This ownership change has been inspected and the packaged silent smoke remains the integration gate; physical output continuing through hidden/minimised windows has not been qualified.

Worker replies are posted to the message thread behind a guarded component reference. The browser remains alive throughout shutdown. Closing first rejects admission and discards queued operations, then waits asynchronously while the worker retires the UI host, offline rendering and output runtime. Only after the driver has been destroyed with the message loop still running does the app quit and join the finished worker. This avoids blocking the message thread while a worker waits for JUCE's synchronous device lifecycle calls.

## Local data and fresh startup

Default application data lives in `~/Library/Application Support/Animal Atlas`. `voices.sqlite3`, `soundscapes`, `exports`, and `settings.json` belong to this instrument. No repository or collector database is automatically attached. `--state-dir ABSOLUTE_PATH` provides a separate instance for validation; `--library ABSOLUTE_PATH` optionally chooses an explicitly supplied read-only wildlife library.

The native toolbar provides:

- **Choose wildlife library**: opens an explicitly selected regular database through the existing read-only library adapter. Invalid selections leave the current host in place.
- **Scene only**: detaches the wildlife library, turns listening off and reloads truthful capabilities/state.
- **Import soundscape**: fully loads and verifies a selected `.atlas-scene`, then publishes a new opaque named copy with the existing exclusive sealed scene writer. The source is preserved; existing scene files are never replaced.
- **Show app data**: reveals the application's own data folder.

Import refreshes the visible UI and disables listening. An independent offline render already in flight continues into app-owned exports, while the refreshed view resets its selected job; persistent job history is a later milestone.

A fresh state directory starts in scene-only mode and can import a soundscape without any wildlife database. A missing or invalid saved library produces a visible status and retains the saved path, while scene-only operation remains available. The instrument never changes an owner's collector state.

A retained interprocess lock is keyed by the canonical application data path. Two applications cannot write the same state; distinct explicit state directories can coexist. State JSON is bounded to 8 KiB and has an exact versioned field set. Invalid state is retained and reported; the session does not silently overwrite it. Valid library and output selections are persisted off the message thread using an exclusive temporary file, flush and atomic rename. Output preferences preserve explicit disconnected device IDs and never enable listening on launch or substitute another device. Output preference changes are saved only after a successful native `setAudioDevice` reply.

Completed WAV links already carry opaque artifact IDs in the React UI. The injected script intercepts those links and opens the native save chooser. The worker maps only a 32 character lowercase hex ID to a regular app-owned `<id>.wav`, and copies it to a new user-selected destination. Existing files are never overwritten. Managed artifact storage is not a download destination, and raw file paths are never exposed as webview URLs.

## Verification entry points

`tests/standalone_bridge.test.mjs` runs with Node's built-in runner and checks bounded admission, UTF-8 limits, correlated replies, malformed/late replies, abort/timeout/page cleanup and opaque native WAV link routing. `tests/standalone_support_tests.cpp` uses disposable state to check state retention/strict parsing, symlink rejection, local resource MIME, failed import publication, export containment and no-overwrite behavior.

`tests/standalone_smoke.py --binary "/absolute/path/to/Animal Atlas.app/Contents/MacOS/Animal Atlas" --seconds 3 --evidence /absolute/path/receipt.json` launches the actual app in disposable state. The embedded WebKit evaluates the production UI DOM and calls through its actual injected transport. It waits for React to render, opens the actual Audio settings popover, checks its visible device/buffer catalogue against native discovery, and confirms discovery leaves output off. It records capabilities, truthful disabled audio, native requests, repeated transport/audio rounds and shutdown completion. It never enables physical output.

Adding `--scene /absolute/path/to/verified.atlas-scene` imports a verified copied scene and exercises native recall, offline render polling and the real intercepted WAV link route to an isolated new destination; the harness checks the stereo WAV and copied scene. App-only automated flags are `--smoke-test RECEIPT`, `--smoke-seconds N`, `--smoke-scene SCENE`, and `--smoke-export DESTINATION`. These select a noninteractive save destination only in explicit smoke mode. They do not supply demo events or bypass the native request adapter.

A successful smoke is evidence of packaged WebKit rendering, native bridge, scene-only operation and silent offline rendering. Physical-device listening quality, owner listening approval, distribution signing/notarisation and DAW hosting remain separate evidence.
