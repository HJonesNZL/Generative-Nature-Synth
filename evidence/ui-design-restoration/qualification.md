# UI design restoration

The current interface restores the original Claude handoff's proportions and presentation. Atlas's globe is again the dominant element; normal guidance and project controls no longer consume the workspace above it. Sound Studio keeps its envelope and knobs prominent. Secondary explanations are accessible under Project, Map details and local Help/Details disclosures. Connection/audio faults remain visible.

The geometry regression measures a 554.6px globe canvas at 1440×900, 454.6px at 1280×800, and 506.6px at 1400×852 (the native content size). The rendered globe at 1440×900 is approximately 530px in diameter, close to the original reference. Screenshots use explicitly synthetic fixtures, not new wildlife collection. Both Atlas and Studio were inspected against `design/claude-handoff` references.

Project's editors stay mounted while closed. Keyboard Enter/Escape works, focus returns to the trigger, composition drafts survive closing and screen changes without accidental application, and export polling continues. Error tests verify composition failures appear and device loss stays visible even when Project is closed. [Layout receipt](results.json).

TypeScript and 37 unit tests pass. Production/demo builds retain their separate sources, local assets, licences, CSP and lazy globe. [Build log](build.log), [unit log](unit-tests.log). Browser integration coverage and exact commands are in [browser validation](browser-validation.json).

The refreshed app passed the actual WKWebView/native smoke with device discovery silent, verified scene recall, 24-bit stereo 48kHz WAV rendering/save, and clean shutdown. [Receipt](packaged-smoke.json). The ZIP was extracted and signature verified; its hashes and exact bundled UI manifest are in [artifact identity](artifact.json). Native audio/DSP source is unchanged. No physical listening, new wildlife collection or native screenshot capture is claimed.

The new local app is `Releases/Design Refresh/Animal Atlas.app`; the previous standalone beta remains intact. Close an older running copy before opening this one because both use the same protected application state.
