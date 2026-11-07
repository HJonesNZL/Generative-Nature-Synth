# Generative Nature Synth / Animal Atlas

Open **Releases/Live Sonification 0.7.1/Animal Atlas.app** for the current instrument. In Atlas, choose **Collect → Start live updates**, then enable **Live listening** to hear incoming detections. [Standalone guide](docs/STANDALONE.md)

Development continues on `beta-1`. The packaged instrument uses portable C++20 for collection, species voices, synthesis and audio export. JUCE hosts the local React/TypeScript interface. The root-level Animal Atlas.app and Start Animal Atlas.command launch the earlier Python collection menu.

- [M1 completed: verified native data pieces](docs/milestones/M1_VERIFIED_PIECE.md)
- [Commit checkpoints and validation](docs/milestones/M1_BUILD_LOG.md)
- [Current build plan](docs/BUILD_PLAN.md)
- [Audit and outstanding work](docs/audits/AUDIT_AND_REVISED_BUILD_PLAN.md)
- [Active engineering rules](AGENTS.md)
- [Build and run instructions](README.md)
- [Latest feature brief and Direction H handoff](deliverables/Animal_Atlas_Direction_H_Design_Handoff/ANIMAL_ATLAS_FULL_FEATURE_BRIEF.md)
- [Original handoff](references/original-handoff/ANIMAL_ATLAS_CODEX_HANDOFF.md)

Instrument source lives in native/ and ui/app/. The Python prototype lives in animal_atlas/. All generated concepts are in design/. Your original images are in Design Reference Images/. The root Animal_Atlas_Direction_H_Design_Handoff folder preserves your earlier copy; the deliverables version includes the later audit updates. Dated ZIPs remain historical snapshots.

Private local collection state and saved recordings remain in .state/ and Sessions/ and are excluded from Git. Old build artifacts are preserved in build/pre-relocation-2026-11-07; the working native build is build/native. The move verified all 403 project files and preserved 26 existing destination files; 50 Python tests and 17 native cases passed at the new location.
