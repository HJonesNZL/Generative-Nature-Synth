# Standalone beta and device lifecycle milestone

7 November 2026. The owner authorises autonomous planning, implementation, validation and commits to beta-1. The scope is a usable self-contained macOS standalone beta with the existing C++ instrument and local React UI. No paid dependency, new service, external data collection, active collector change, or wildlife-library write is introduced.

## Outcome and acceptance

An Animal Atlas.app bundle launches by itself without Node, Vite, a terminal server, repository paths or network-hosted assets. Its embedded WebKit screen runs the existing Atlas and Sound Studio against the same native UiHost/renderer. A fresh app can choose a read-only wildlife library or import a verified portable soundscape; no development database is required. Voice definitions, scenes, exports and preferences live in an app-owned Application Support directory. Existing project data is not moved or silently adopted.

The app exposes actual available output devices and buffer choices, explicit apply and Listening/Play controls, actionable unavailable/change status and manual recovery. Selection never enables playback, substitutes another explicit device or resumes sound after device loss. Device preferences survive a restart, including a disconnected explicit selection. The original standalone command-line host and browser development workflow remain functional.

Delivery includes a reproducible build/package command, self-contained SQLite dependency, bundled notices, local ad-hoc signature, package integrity/dependency checks, an installed-copy smoke test and a ZIP. This is a locally usable beta; Developer ID notarisation, public distribution/licensing decisions, another-machine/platform validation and human listening are separate release acceptance, not fabricated completion claims.

## Architecture and ownership

The optional CMake atlas-standalone target is a MACOSX_BUNDLE executable using the installed JUCE 9.0.0 modules and OS WebKit. It links the existing portable storage/playback/audio core; there is no second synthesis engine. A native toolbar handles explicit library selection, scene-only mode, verified import and file destinations. No microphone input is requested.

A single serial control worker owns PlaybackRuntime, VoiceStore, UiHost, app preferences and file operations. The message thread owns the window, WebKit, file choosers and JUCE device lifecycle. A bounded event bridge passes JSON envelopes to the worker and correlated replies to the live browser. Queue and request caps, cancellation, late replies, navigation policy and shutdown are specified in the shell contract. Webview resource requests use a bounded preloaded local asset map; no arbitrary filesystem access or network navigation is exposed. Export copies only an app-owned opaque artifact to an explicit destination and refuses replacement.

Close stops admission, requests worker shutdown and continues the message loop until output callbacks have stopped and worker-owned objects are destroyed. Only then does the app quit/join. This prevents deadlock with JUCE's synchronous message-thread device calls. Multiple instances may not share writable app state; use an instance guard and isolated state for tests.

Portable device catalogue/configuration types are optional OutputDriver capabilities, with bounded enumeration, revision/preparation fences, supported prepared sample rate and truthful active metadata. Configuration changes quiesce output and remain silent. Device IDs come from enumerated metadata and are not file paths. Duplicate names/identity collisions must be refused rather than misrouted. Strings/files/catalogue work never enter the signal-processing callback.

## Playback scope and resource checks

The owner's clarification supersedes the Field skills' live-input constraints. This instrument does not process live audio; no monitored-input latency target or Field microsecond deadline gate applies. Preserve ordinary thread safety and smooth playback without limiting musical scope by effect-oriented budgets. Test sustained generation, finite output, progress, bounded ownership, UI responsiveness, device failures and clean shutdown. Report offline throughput separately from physical audio experience. Never describe an offline timing outlier as an audible failure without evidence.

## Workstreams and integration

1. Shell agent: embedded local UI, bounded bridge, app storage/choosers/import/export, asynchronous close, actual WebKit smoke harness.
2. Device agent: portable catalogue/preferences, JUCE adapter selection/loss/recovery, native DTO boundary and deterministic lifecycle/sustained tests.
3. UI agent: capability-gated Audio settings, explicit apply/recovery and correct transport fences, unit/browser coverage and screenshots.
4. Parent: frozen cross-stream contracts, CMake and dependency packaging, notices, isolated installed-artifact checks, independent review, evidence and detailed commits/pushes.

All agents use GPT-6.1 SOL and disjoint files. Native core changes build with warnings as errors; affected tests and sanitizer/race checks cover changed lifetime seams. Existing scene hashes/audio and export behavior must remain compatible. Actual WebKit smoke must run the bundled JS bridge and DOM; a direct C++ UiHost invocation cannot substitute for that check. Use synthetic copied scenes and disposable state throughout.

## Verified upstream references

Installed JUCE source, not a guessed current API, defines the implementation. Its WebBrowserComponent supports resource providers, injected user scripts and event transport; see [official API](https://docs.juce.com/master/classjuce_1_1WebBrowserComponent.html). The installed LICENSE.md was read in full: JUCE 9 offers AGPLv3 and a commercial licence. This local build adds no purchase and does not change the project's licence or publish a binary release. Include exact upstream notices; public distribution needs its chosen terms resolved. [JUCE legal index](https://juce.com/juce-legal/). The bundled SQLite deliverable is public domain according to [SQLite's copyright statement](https://sqlite.org/copyright.html). Ad-hoc signing establishes local integrity, not Developer ID/notarisation or cross-machine trust.
