# Next milestone: scalable saved-material browsing

Follow the completed project recovery milestone with a focused library usability and responsiveness pass. The existing app can preserve scenes and export audio; finding and preparing older material must remain responsive as collected archives grow.

## Scope

1. Replace bounded first-page-only session lists with explicit cursor pagination and accessible next/previous/load-more controls. Preserve stable selection, source provenance and frozen/interrupted state; expose omitted/unknown counts truthfully.
2. Audit material paging so repeated UI requests do not rebuild an entire timeline or reread unchanged payloads. Measure a named synthetic large library first. Keep any cache bounded, tied to library identity/revision and invalidated on switch; no speculative generic cache framework.
3. Distinguish missing, unreadable and incompatible read-only libraries with clear recovery actions in the existing Project/File disclosures. A failed selection preserves the current library, render and saved files.
4. Add restart/large-library/browser regressions and record time/memory before and after demonstrated optimisations. Keep the globe layout and separate station/individual location semantics.

Portable storage/query logic remains C++20; the serial worker owns database access and React owns paginated view state. No migrations or collector writes are permitted through read-only browsing. Start by freezing request/response cursor contracts and concurrency/selection semantics, then split storage, UI and regression work across GPT-6.1 SOL agents with disjoint ownership. Existing project/scene formats remain compatible.

## Acceptance

A disposable library with thousands of saved pieces can reach a piece beyond the first page, prepare it once, navigate events and switch screens without losing selection. Concurrent changes either advance an explicit revision or force refresh; stale replies cannot replace newer selections. Invalid cursors and unavailable files fail observably. The measured large-library case improves without unbounded memory or regressions in scene recall/export/recovery. The exact packaged app passes browser and native tests; no hardware-listening or new-source-availability claims are inferred.

Physical listening, output hotplug/sleep/wake qualification, source access/permissions and later VST3 hosting remain separate roadmap work. This milestone does not turn an unverified provider into a live feed.
