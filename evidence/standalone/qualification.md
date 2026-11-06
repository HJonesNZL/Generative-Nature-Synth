# Standalone beta qualification

7 November 2026. **The bounded standalone engineering milestone is complete.** The app packages the existing native instrument and offline React UI, provides device selection and buffer preferences, imports verified scenes, saves rendered WAVs through a native dialog, and closes without blocking the message thread during device retirement. This is a local Apple Silicon/macOS 15 beta, not a public release qualification.

## Delivered behavior

The app works without Node, Vite, repository paths, remote assets or a configured wildlife database. Fresh startup explicitly offers scene-only operation and does not poll an absent library. An explicitly selected wildlife library is opened read-only; app state lives separately in Application Support. Imported recipes remain self-contained. No owner collector, database, recording or design archive was changed.

Device changes use catalogue/preparation/transport revision fences. Apply turns playback off; explicit unavailable devices remain selected after restart and never silently fall back. Discovery reports actual CoreAudio outputs without opening audio. Listening and Play remain explicit. The serial native worker renews the playback lease independently of hidden webview timers; service checks for real stalls before renewal. This is ordinary generative playback behavior. Field live-input latency or microsecond deadline budgets are not acceptance gates.

The bundled UI and SQLite dependency are sealed with a local ad-hoc signature. Exact UI/dependency hashes, native source identity, executable hash, ZIP hash, minimum OS and dependency inventory are in [artifact.json](artifact.json). The ZIP was extracted and its signature and executable verified. The optional visual-inspection connection stalled after the temporary app had exited; no native screenshot or Finder-launch visual claim is made. Browser screenshots were inspected separately and are labelled by their fixture receipts.

## Verification

| Check | Result and evidence |
| --- | --- |
| Release native build and tests | Warnings as errors; 22/22 CTest suites, including standalone support, device protocol, scenes, existing audio/DSP compatibility and quiet real-JUCE lifecycle. [Log](native-tests.log), [JUnit](native-tests.xml). |
| Concurrency and memory | Independent TSan runtime 19/19; ASan/UBSan runtime 19/19 and device protocol 4/4. Per-binary source manifests and commands in [sanitizer receipt](device-sanitizers/device-sanitizers.json). No diagnostics; Apple leak detection unavailable. |
| Sustained generative runtime | 51,782,400 synthetic frames over 24 fault/recovery epochs, about 17.98 minutes of simulated audio. Finite output, progress, quiet recovery and counters verified. This is not 17.98 minutes of physical listening. |
| Frontend units | 37/37, including bounded DTOs and optional library capability. [Log](frontend-tests.log). |
| Bridge | 2/2 groups cover capacity, UTF-8 bounds, correlation, malformed/late replies, abort/timeout/page exit and opaque WAV routing. [Log](bridge-tests.log). |
| Device interface | 12 static production browser checks and 8 real native-protocol/fake-output checks. A refresh race found by integration is fixed and regression-tested. [Static](audio-devices-browser.json), [native protocol](native-audio-devices-browser.json). |
| Fresh scene-only interface | Eight checks, including no absent-library queries/retries, preserved scene material and usable Studio. [Receipt](scene-only-browser.json). |
| Actual embedded app | WKWebView renders production React, opens Audio settings, displays native CoreAudio devices/buffers and leaves callback count zero. Imported scene recall renders and saves stereo 24-bit/48 kHz PCM with exact recipe identity. [Scene journey](packaged-scene.json). |
| Independent app copy | Copied outside the repository and run from a disposable working directory for 60 seconds. Repeated actual WebKit/native transport and audio queries, scene render/save, and clean shutdown. [Receipt](installed-app.json). This run stayed silent. |
| Restart and ownership | Missing library plus unavailable explicit device retained across restart; duplicate-field settings preserved and explained; second process denied the same writable state; reopening after lock release succeeds. [Receipt](lifecycle.json). |

## Findings resolved during integration

- Installed SQLite requires macOS 15; the app declares that minimum rather than advertising unsupported older systems.
- Repackaging now safely replaces its own read-only Homebrew dependency copy.
- Both worker admission and frontend replies reject retired page generations after library/view changes.
- Successful scene import turns Listening off before refreshing the interface.
- Device refresh disables drafts until the current catalogue arrives, preventing a stale selection from being silently overwritten.
- Scene-only capability prevents misleading library retry and data-loss promises.
- The independent existing RIFF oracle validates extensible 24-bit PCM on Python 3.9; no extra audio parsing dependency was added.
- The native worker supplies the standalone heartbeat, so minimized webview polling is not the playback-liveness owner.

## Remaining product acceptance

Physical hotplug, hardware buffer changes, sustained device listening, hidden-window playback, sleep/wake and another-machine operation have not been qualified in this milestone. The synthetic fixtures exercise selection/failure/recovery semantics, not hardware behavior. Human timbre/ensemble approval, whole-product resource profiles and VST3/DAW integration remain open. Local ad-hoc signing is not notarisation; public distribution must resolve its licence/signing route. No paid service, account or dependency was introduced.

Import refreshes the UI; an already-running independent export can finish in app storage while its selected job panel resets. Persistent export history, complete backup/restore and friendlier library management belong to the next milestone. Device IDs use JUCE type/name metadata; renaming a device requires reselection. The app does not yet own collection, and regional activity/source-health displays remain capability-limited.
