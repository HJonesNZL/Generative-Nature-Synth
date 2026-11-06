# Backend implementation decision, 6 November 2026

Historical implementation record. [BUILD_PLAN.md](BUILD_PLAN.md) supersedes its future-work ordering and packaging proposals; retain the operational Python collector while verified native audio and safe migration are built. [The audit index](audits/AUDIT_AND_REVISED_BUILD_PLAN.md) tracks current repairs and open gates.

The production-language decision is now portable C++20 following the owner's instruction. This file describes the operational Python prototype. The new C++ core and React/JUCE boundary are documented in `native-react-architecture.md`; acquisition and write-side session logic have not been fully migrated. Existing collection remains running without a Python/native writer handover.

Version 0.2 extends this design with frozen recording sessions, portable local bundles, deterministic event replay and a local menu launcher. See `sessions-and-replay.md` for the current session contract and validation; no audio synthesis or atlas GUI is included.

## Zero cost

The owner requires zero-cost software and data collection. This slice uses only the installed Python standard library, SQLite, local storage, and two currently unauthenticated public data routes. No API key, cloud account, trial, paid map SDK or subscription is needed. Runtime installation needs no pip operation. These public routes have no established service guarantee. A new access barrier stops collection; there is no automatic upgrade or paid fallback.

Data cost and legal permission are separate. The registry explicitly leaves commercial access/reuse unresolved. No organisations have been contacted and nothing has been published. Current tests are bounded local research requests. Broader distribution must use a compatible permitted zero-charge source route; if unavailable that connector remains unavailable.

The earlier JUCE/C++ collector proposal is revised: a Python collector is fast to validate, isolated from audio, and avoids a paid framework dependency for this backend. A versioned JSON boundary allows a later native C++ instrument without rewriting acquisition. A distributed app will eventually need an independently packaged Python runtime or a port; do not assume customers have Xcode Python. Future UI/audio dependencies must be reviewed against the zero-cost requirement rather than inheriting a JUCE commercial licence assumption.

## Ownership and data flow

Each source owns an independent worker and backoff. Adapter batches are bounded to 1,000 BirdWeather detections and 1,000 shark subjects. The SQLite store serializes short write transactions. A transaction stores revisions, latest-event references, source checkpoint and successful-poll evidence before any client sees the change. First-seen time survives corrections. Duplicate polling generates neither a revision nor a new trigger. A correction reverting to a previously seen value is still a new revision of the latest state.

First-run baseline completion requires exhausting pagination. Changing geographic scope/lookback restarts baseline suppression. Baseline does not claim complete source history. Identity is source-namespaced; station and provider-subject identity never imply reconciled animals/deployments.

The loopback API reads durable rows. Long-poll requests use monotonic deadlines and bounded batches instead of a memory broadcast queue. Sequence acknowledgements belong to consumers, not to the server. The collector runs even if no application client is connected. No audio process or DAW is required.

Use UTC for event timestamps and first-seen times. These clock values can jump with system-clock correction; poll waiting and long-poll expiry use monotonic waiting. Source timestamps without zones or more than five minutes in the future are rejected, not assigned invented UTC values. Missing coordinates are preserved. Invalid records increment counters; a replayable raw quarantine is not implemented yet.

## Storage and security

Schema v2 uses `metadata`, `sources`, `events`, `revisions`, `polls`, `sessions`, and `session_events`. It migrates existing v1 collections without discarding records. Migrations reject newer unknown versions. SQLite WAL and FULL synchronization are enabled; contexts close their connections promptly. An OS advisory lock prevents two CLI collectors against the same state directory. Raw source records are stored only alongside accepted event revisions; full map configuration/media/token fields are never retained. TLS verification remains enabled; provider redirects are denied.

512 MiB is an admission soft limit, not a filesystem quota. Writes also require 64 MiB free. There is no retention/pruning command or consistent backup/export utility yet. Do not copy the active SQLite main file as if it were a complete backup. Stop the collector cleanly and preserve its whole state directory for local archival. Never share the state folder: it includes the API token and source data with unresolved redistribution permissions.

## Remaining engineering work

1. Add provider-specific reconciliation/backfill and richer poll/failure coverage reporting. Rolling BirdWeather windows are best-effort and globally capped.
2. Add full NZ track-history access only after endpoint schema/usage review; reconcile animal versus deployment from evidence. Current connector is snapshots only.
3. Add retention, consistent full-database backup and permission-confirmed redistribution. Frozen sessions and portable local archives are implemented; protect them when adding retention.
4. Add replayable bounded rejection quarantine, schema-drift alarms, source health ages and error history.
5. Implement GUI reconnect/cursor persistence and then a bounded native audio-worker bridge. No audio/GUI software exists in this slice.
6. Add permission-confirmed international connectors rather than claiming the whole catalogue works.
7. Package an owned runtime; add macOS background lifecycle/sleep controls and 48–72 hour qualification.

This version is a runnable backend proof with real data, not a claim of complete global coverage, production durability under every failure, source redistribution rights, or deterministic audio.
