# Independent globe preview review — 7 November 2026

Reviewer did not author the globe implementation. Scope inspected: `ui/app/src/screens/shared/mapMarkers.ts`, `screens/shared/useMaterialView.ts` (the actual shared path), `screens/atlas/MapStage.tsx`, `screens/atlas/LeftColumn.tsx`, `components/map/Map2D.tsx`, `app/eventStore.ts`, `tests/map-markers.test.mjs`, native `ui_host.cpp` event translation/catalog/capability methods, `Start Atlas UI.command`, and `docs/milestones/GLOBE_PREVIEW.md`. Read-only qualification except this evidence file; temporary probes used synthetic SQLite only.

## Confirmed P2: an individual fix can become a fictitious receiver

The native catalogue creates a station for any event with nonempty `station_id` and coordinates, without requiring `location_kind` to describe a receiver. The UI then aggregates all events sharing that station ID as loaded detections. Optional station metadata is allowed on a valid GPS fix. Therefore an individual measured position can be presented as a reported receiver location, with a detection count, in addition to its correctly identified GPS symbol.

Reproduction: create a disposable Store, ingest `event('fixture','gps-one','2020-01-01T00:00:00Z','gps_fix','individual_fix',40,-70,station_id='fixture:site',individual_id='fixture:tag',provenance_mode='demo')`, then send native `ui-host --db TEMP` a protocol-1 `getCatalog` request. The verified response contains:

```json
{"stations":[{"id":"fixture:site","latitude":40,"longitude":-70,"positionDescription":"Reported station position; individual position unknown"}]}
```

Passing that catalogue and event into `buildMarkers(..., null, true)` produces both `station:fixture:site` labelled `fixture:site receiver, 1 loaded detections` and `fix:fixture:gps-one` labelled as a GPS fix. Both native acceptance/catalogue and UI-marker parts were independently executed successfully. Fix the native station catalogue boundary and restrict station aggregation/selection to receiver-location records; a regression should retain the GPS marker and omit the fictitious station. This review did not change product files.

## Verified scope and limits

Existing UI unit suite passed 12/12 using bundled Node with `--experimental-transform-types --test tests/*.test.mjs`. Its map regressions cover more than forty measured fixes, receiver aggregation, selected/highlighted cap priority, offset/sub-millisecond observation ordering and receiver fallback outside the sampled catalogue. Passing existing tests does not cover the counterexample above.

Timestamp ordering compares parsed UTC milliseconds plus the remaining microseconds, so timezone-offset display order does not rely on lexical comparison. Newest loaded observation is explicitly labelled and is not described as source freshness. Stale connected-source presentation is separate, but native capabilities do not currently expose source health; no actual freshness evidence was established.

Loaded-vs-library limits are prominently disclosed: 2,000 retained records, native initial newest-200 revisions, catalogue sampled from 2,000 records and at most 400 displayed map symbols or a lower host cap. Scope defaults to loaded records and can switch to the current window. Species counts use the loaded buffer. Selected marker priority precedes highlighted/normal/dim symbols. SVG markers support Enter/Space and a native-button list provides an additional keyboard selection route. This is static/logic inspection and unit evidence, not a completed accessibility audit.

The existing `Start Atlas UI.command` can launch the early preview with pinned dependencies, native executable and an existing readable library; it also accepts `ATLAS_LIBRARY` for a disposable fixture. It starts the localhost Vite/native read-only bridge, without starting collection. Its console's “no audio engine yet” wording is historical: a separate offline native renderer now exists, while UI capabilities still honestly report `audioAvailable:false` and omit transport/render/voice methods. In-app playback remains unsupported.

## Not run / qualification boundaries

No owner library or collector was read or changed. No live-provider requests, source-health/freshness qualification, owner listening, DAW session, native webview, screen-reader session, GPU/long-duration performance or actual owner-launcher run was performed. Existing browser evidence described in GLOBE_PREVIEW.md was inspected as a claim, not independently rerun in this review. The confirmed receiver/fix defect remains for parent-owned repair and regression verification.

## Resolution checkpoint (added after the independent review)

Commit `0779145` fixes the native receiver catalogue and map aggregation/selection boundaries. The retained `receiver-regression-before.log` reproduces the failure; final native UI tests include the GPS-with-station-metadata case and pass in all three native configurations. The prior `globe-checks.json` records 13 UI unit tests and nine browser groups after that repair. The original review above remains historical evidence, not a statement that the catalogue defect is still open. A later frontend audit found the same distinction was still wrong in Inspector wording; that separate repair and its browser evidence belong to the frontend readiness checkpoint.
