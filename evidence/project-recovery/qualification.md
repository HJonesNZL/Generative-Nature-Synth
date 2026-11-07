# Project recovery and draggable NZ detail — 7 November 2026

Implemented and pushed on `beta-1` with GPT-6.1 SOL agents for map interaction, portable project storage/export inventory, and native shell/integration tests. The parent integrated, reviewed and packaged the result. Map checkpoint: `bac4736`; recovery implementation: `a447aafbf0aef20f3ed21718b7d3ed73e77f02d6`. The final evidence commit contains this ledger.

## Delivered behaviour

- The NZ detail stays inside its fixed circular frame at the original enlarged scale. Drag mouse/touch/stylus to move through the country; arrows, Shift+arrows, Home and Reset view work. View persists between Atlas and Studio. Markers, coastlines, region outlines and uncertainty share one geographic projection; the main globe is unchanged.
- Exact sealed scene export; selected-scene plus saved-voice `.atlas-project` backups; full manifest/hash/schema/semantic verification; restore only to a new nonexistent state folder under managed local IDs. Existing state and original files survive conflicts/failures. Restored scenes recall and render without a wildlife library.
- Compact File / Project actions and on-demand saved audio history. Final WAVs survive restart discovery; corrupt/partial files are excluded. Native save/reveal use managed IDs. Library changes preserve independent rendering, and a UI reload recovers progress/cancellation. Diagnostics retain actual library/scene-only state independently of the last action.
- Parent aliases such as macOS `/var` resolve in the shell worker; the portable archive boundary still rejects symlinks/traversal and never follows external SQLite WAL files during import. No paid service/dependency or owner collector changes were introduced.

## Artifact

Use `Releases/Project Recovery/Animal Atlas.app` or the adjacent `Animal-Atlas-Project-Recovery-arm64.zip`. The local app is Apple Silicon/macOS 15+, with bundled production React assets, SQLite and notices. It requires no Node/Vite/web service at runtime. Earlier release folders are preserved. It is locally ad-hoc signed, not a public notarised release.

Native source SHA-256: `ec481881ad3c9e6afc96450fff70e5c143c8839bf98a431425fade13a84a5edb`. ZIP SHA-256: `ab9941ac48d325a9bd83cf8307ad06ad5645145caa9a08340a21ee4b398760ec`. [Exact artifact manifest](artifact.json) includes the binary and every bundled resource/dependency hash. The final source hash changes from earlier test builds only where recorded development edits/formatting changed; no DSP synthesis change was made in this milestone. New WAV metadata includes the saved scene name.

## Executed evidence

| Check | Result and scope |
|---|---|
| Native Release build | Project warnings as errors; all 25 CTests pass, including new archive/export/UiHost workflows and shell support. [Log](native-release-tests.log). |
| Sanitizers | Fully instrumented ASan + UBSan archive/export/UiHost project tests, 3/3 pass. [Log](native-sanitizer-tests.log). |
| UI | TypeScript passes, 43/43 unit tests pass; production and demo bundles pass isolation/CSP/local-asset checks. [Unit log](ui-unit-tests.log). |
| Native bridge | 4/4 tests pass, including opaque scene export and WAV reveal events. |
| NZ interaction | Real Chrome mouse/touch/pen and keyboard; drag beyond frame, click threshold, cancelled/lost capture, bounds/South Island access, screen persistence, 1280×800/1440×900/1400×852 and independent 2D fallback. [Receipt](loupe-results.json). |
| Native browser | Voice edit→scene capture→exact scene download→recall→identical WAV; restart native worker and recover both completed exports with identical downloads. [Receipt](native-browser.json), [screen](export-history.png). No physical output enabled. |
| Render UI recovery | Recover running export with collapsed Project badge; progress and Cancel work; cancelled job stays cancelled after reload with no fabricated WAV. Demo scene export stays hidden. [Receipt](export-recovery-browser.json). |
| Archive adversity | Missing/duplicate/over-capacity files, malformed schema/manifest/paths, resealed semantic corruption, invalid integer lengths/ordinals, truncation, symlinks, destination conflicts and committed WAL-versus-unsaved draft separation. |
| Write failure/interruption | POSIX child RLIMIT_FSIZE and SIGXFSZ force actual staging write failures/interruption; no partial destination, original scene/backup/voices/WAL unchanged. Ordinary failures clean staging; killed process can leave unreferenced staging. This is not an actual full-disk or power-loss test. |
| UiHost lifecycle | Active render survives scene-only switch and scene import; corrupt replacement library preserves current host; cancellation publishes no WAV; completed history survives host recreation. Restored scene hash and rendered WAV bytes exactly match the original. |
| Actual packaged WebKit | Native scene/WAV links, backup→inspect→restore worker paths and disabled-output/device UI verified. [Receipt](packaged-smoke.json). |
| Extracted release ZIP | Extracted outside the repository and launched from that temporary folder; strict code-signature verification, scene/WAV/backup/restore and all six lifecycle cases pass. [Smoke](installed-zip-smoke.json), [lifecycle](installed-zip-lifecycle.json). |
| Actual application lifecycle | Missing library/preferences retained over restart, invalid settings retained, simultaneous state writer rejected, lock released after exit. [Receipt](packaged-lifecycle.json). |

## Limits and next work

All data/voices/state used by these tests are disposable synthetic fixtures. No owner wildlife database was written and no collector was stopped or activated. Project backups deliberately exclude unsaved drafts, raw wildlife libraries, application/device settings and exported WAVs; save these separately where needed. History validation checks structure/recipe consistency, not PCM cryptographic authenticity or audible quality.

The menu's restore preview/new-window launch is implemented; automated smoke exercises the same verification/publication worker while substituting explicit destinations and skipping child-window launch. It does not automate every system chooser click. Hardware listening, minimised-window physical playback, hotplug/sleep/wake, actual disk exhaustion/power loss, Windows/Linux publication branches, external-machine use and VST3/DAW hosting are not newly qualified. These are separate product release gates, not inferred from silent offline tests.

The next bounded milestone is [scalable saved-material browsing](../../docs/milestones/NEXT_LIBRARY_BROWSING.md), with explicit pagination, bounded read-only preparation and measured large-library responsiveness. [Quick start](../../docs/STANDALONE.md), [project format](../../docs/engineering/PROJECT_ARCHIVE.md), [export history contract](../../docs/engineering/EXPORT_HISTORY.md).
