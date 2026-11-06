# Restore the original interface hierarchy

The owner's 7 November correction identifies two regressions: the globe became much smaller and the interface accumulated excessive text. The implementation reference is the original Claude handoff under `design/claude-handoff`, particularly Atlas and Studio at 1440×900.

## Changes

- Replace permanently expanded soundscape/composition rows with a compact **Project** disclosure in the header. Both editors remain mounted while closed, preserving drafts, edit finalizers and background export polling. Escape closes the panel and returns keyboard focus; errors from its operations reopen it. Export progress/completion remains visible on the trigger.
- Remove normal capability/tutorial banners from the main workspace. Put setup/playback guidance inside Project; leave connection and audio failures visible.
- Return map space to the globe. Put record scope, counts, newest observation and coverage explanations under **Map details**. Keep concise legend and keyboard location browsing. Fit the sphere to the shorter canvas edge in narrow windows without resetting user zoom.
- Preserve Studio's envelope/knobs and native behavior. Move secondary voice/preset/region explanations and detailed coordinates into disclosures; keep meaningful state, source/provenance and actionable failures available.
- Keep the local cream/forest-green visual language, rules, typography and three-column layout. No DSP, data or audio-device behavior changes.

## Verification

`ui/app/tests/design-restoration-browser.mjs` compares Atlas/Studio geometry at desktop sizes, captures review images, verifies keyboard access, close/reopen draft preservation without implicit application, and confirms playback errors stay visible when Project is closed. Existing native soundscape/composition/spatial journeys are updated to use the relocated controls. TypeScript, unit tests, production/demo builds and offline separation remain required.

The native app bundles the updated production UI. A new deliverable is published under `Releases/Design Refresh`, preserving the previous beta copy. Close an older running copy before opening the refreshed app because both correctly use the same application data lock. Test files use synthetic data/disposable state; none of the displayed test species are newly collected observations.
