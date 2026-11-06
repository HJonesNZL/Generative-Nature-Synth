# Generative Nature Synth / Animal Atlas

This folder is the canonical workspace from 7 November 2026. All future development continues here.

- [Current build plan](docs/BUILD_PLAN.md)
- [Audit and outstanding work](docs/audits/AUDIT_AND_REVISED_BUILD_PLAN.md)
- [Active engineering rules](AGENTS.md)
- [Build and run instructions](README.md)
- [Latest feature brief and Direction H handoff](deliverables/Animal_Atlas_Direction_H_Design_Handoff/ANIMAL_ATLAS_FULL_FEATURE_BRIEF.md)
- [Original handoff](references/original-handoff/ANIMAL_ATLAS_CODEX_HANDOFF.md)

Source lives in native/ and animal_atlas/. All generated concepts are in design/. Your original images are in Design Reference Images/. The root Animal_Atlas_Direction_H_Design_Handoff folder preserves your earlier copy; the deliverables version includes the later audit updates. Dated ZIPs remain historical snapshots.

Private local collection state and saved recordings remain in .state/ and Sessions/ and are excluded from Git. Old build artifacts are preserved in build/pre-relocation-2026-11-07; the working native build is build/native. The move verified all 403 project files and preserved 26 existing destination files; 50 Python tests and 17 native cases passed at the new location.
