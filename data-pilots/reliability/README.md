# Existing-source reliability pilot

Owner decision, 7 November 2026: retain the live listening/detection stream; other source expansion should prioritize **individual-animal location tracking**. Do not treat acoustic stations or general species occurrences as individual animals. The earlier GBIF-first expansion ranking is superseded for this intended direction; Movebank-style genuine tracking is the priority, with general occurrence layers optional/deferred. This pilot changes no source or product behavior.

## Scope and execution

`check_reliability.py` is a disposable acceptance/characterization harness importing the **existing temporary Python reference** `Collector`, `Store`, `BirdWeather`, `NZSharks` and `Transport`. It is not a second production collector and has no polling service or network-enabled CLI. Production migration remains portable C++20.

```sh
python3 -B data-pilots/reliability/check_reliability.py
```

All responses are synthetic `fixtures.json` or deterministic derived responses; clock is fixed to 2026-01-01 12:00 UTC. Real urllib opener calls fail before network IO. Scripted transports enforce a per-scenario maximum request budget (0–10); BirdWeather page caps are explicitly exercised. Each test creates/removes its own temporary SQLite library. No owner data, source credentials, paid dependency, media, provider request, process management or collection change occurs. `results.json` records current source hashes, counts and counterexamples. A passing run means **observed characterization matches**, not that the known defects are fixed or every proposed acceptance gate passes.

Component contract: test worker owns synthetic clocks/transports/temp storage; input is scripted provider responses/errors/cancellation; output is bounded assertions and a local JSON report. Unexpected requests, exhausted script budgets, schema deviations or assertion failures fail the run. There is no audio-thread consumer. Verification: 15 tests, 0 failures/errors; six retained counterexamples. Tests should fail after relevant production changes until the corresponding expected observation is deliberately replaced with the new acceptance invariant.

## Per-source capabilities, not one universal live feed

| Current adapter | Supported meaning | Unsupported or unverified |
|---|---|---|
| BirdWeather | Rolling acoustic species detections at receiver/station positions; stable provider detection IDs; local revisions and first receipt timestamps. Default query is NZ bbox, ten-minute lookback, ≤100 records/page × ≤10 pages, polling ≥60s. | No animal identity, movement fixes, publication cursor, snapshot-consistent pages or verified historical archive. A rolling poll cannot recover all late uploads or an outage beyond its lookback. Do not infer ecological absence from an empty response. |
| NZSharks | Public-client **latest-position snapshots** per provider subject, ≥900s polling; source observation timestamps; same subject+timestamp coordinate changes become local revisions. Coordinates remain reported, accuracy unknown. | No continuous positions or intermediate fixes, complete history, verified publication lag, stable individual/deployment reconciliation or provider deletion protocol. Normalization intentionally retains `provider_subject_id` and display name while `individual_id` and `deployment_id` stay null. A provider subject can be reused; never manufacture an individual by joining names or assuming tag permanence. |

Existing code uses public HTTPS routes without API keys and records zero-charge/unresolved-permission metadata. That is a code-path fact, not a current availability test, permanence promise, licence clearance or guarantee that a source remains free. This offline pilot verifies neither upstream access nor historical rights. Do not change existing working routes based on simulated evidence.

## Demonstrated defects versus semantic gaps

| Fixture/report ID | Current reproduced result | Proposed integration acceptance |
|---|---|---|
| `shark_cancel_before_fetch` | A pre-cancelled NZSharks fetch performs bootstrap and config requests before checking cancellation. | Check stop before every request. A pre-cancelled operation sends zero requests. |
| `cancel_after_final_response` | BirdWeather receives cancellation during final response but returns a complete batch. Collector has no subsequent publication guard. | Recheck after response and before commit. Define cancellation generation/publication linearization; do not call a socket immediately cancellable merely because waits are interruptible. |
| `stale_retry_after_success` | After 429 then success, errors reset but persisted `retry_delay_seconds` remains 120. | Clear retry state/deadline on success; health must distinguish historical throttling from active backoff. Worker return delay already resets to zero, so this is a stale-status bug, not evidence it still waits120s. |
| `all_invalid_page_exhausted` | Exhausted page with one invalid record sets baseline complete with zero accepted records. Invalid count is retained. | Distinguish pagination exhaustion, usable-data quality, baseline policy and coverage. This is a degraded-data semantics gate; `complete` currently means exhausted, so it is not proof of ecological silence or an unconditional adapter bug. |
| `outside_window_record` | A server response containing a year-old observation is accepted despite current requested rolling window. Fetch success is current while newest observation is old. | Choose explicit out-of-window retention/quarantine policy; preserve observed, first-received and ingested times. No invented publication timestamp or freshness from successful HTTP alone. |
| `no_upstream_delete_protocol` | Missing subject in a later latest-fix response leaves earlier history intact. | Absence from a snapshot is not deletion. Define explicit source withdrawal, privacy suppression and subject-not-current states; historical retention and redisplay rules require upstream terms. Do not fabricate tombstones or remove all prior fixes on temporary disappearance. |

Additional verified properties: initial/restarted baseline does not pulse; corrections never pulse and retain original first receipt; duplicates do not create revisions; partial capped baseline stays partial; repeated cursors and GraphQL partial-error responses reject without committing accumulated pages; provider error bodies are not persisted as health messages; old complete-poll time flags a possible unrecoverable lookback gap. Initial records after a completed baseline can be live-eligible even when their observation is old: this is **new receipt**, not a claim of fresh animal presence.

## Integration points and remaining acceptance work

- `animal_atlas/sources.py::Transport.json`: bounded response bytes (8MiB) and per-operation timeout15s; redirects rejected; redacted HTTP/network/schema error codes. Numeric/HTTP-date Retry-After is finite and clamped0–86400s. Timeout does not establish a strict end-to-end poll deadline; no cancellation token reaches an active read. Native transport needs monotonic deadline, bounded parsing and cancellable worker request ownership.
- `BirdWeather.fetch`: in-memory pagination cursor is reset each poll; max-pages output records and invalid count remain bounded. Windows are relative per request, not frozen snapshots; overlap cannot prove completeness. Baseline/provenance should stay independent of musical admission. Historical access needs a separately verified provider contract, not this rolling endpoint relabelled history.
- `NZSharks.fetch`: bootstrap URL/server/public-name pinning fails closed on changed installations; max1000subjects. Preserve latitude/longitude order, point observation time, provider subject and unresolved identity/deployment. Native individual tracking adapters need explicit study/individual/deployment identifiers plus retained historical point timestamps and gaps.
- `Collector.poll/_worker`: each source has one worker; capped exponential retry uses60s base up to3600s, honors larger Retry-After, waits max(configured interval,retry)+0–3s jitter through the stop event. Add deterministic per-source scheduler state tests when migrating, including monotonic-clock jumps, concurrent source independence, authentication denial/manual retry, cancellation generation and poll deadline. Generic internal/storage failures currently return60s without setting a new retry-delay field.
- `Store.ingest/source/status`: event revisions and checkpoint commit together in one transaction; receipt/ingestion differ from observation and unknown publication lag remains null. Full source-health DTO should expose attempted/successful fetch, latest valid observation, current retry/degraded state, scope/cap/invalid count and measured-at time. Test privacy withdrawals without deleting immutable saved material silently; state what can still be displayed/exported.

Privacy/reuse: BirdWeather station locations are not individual fixes; shark exact public coordinates are not permission to redistribute histories. Current adapters do not implement a privacy withdrawal feed. Preserve unresolved permissions, attribution and uncertainty. Do not combine coarse/hidden data to reconstruct sensitive positions. Any future Movebank integration must retain the study's access/embargo/licence and deployment boundaries; historical package import is distinct from polling active fixes.

These are bounded synthetic tests, not live-source, current endpoint, long-duration, network jitter distribution, historical-completeness or ecological-validity qualification. Keep the six current counterexamples visible until integration fixes and new regressions demonstrate the accepted behavior.

## Correction to the earlier evidence review

The earlier review's “qualified copied Debug executable” inference was wrong. Mechanical recheck: `build/m2-qualified-743cadf5898d/atlas-native` and `build/native-release/atlas-native` both hash to `af0411290a858472cd96ea86b61d4032a54a28e2b4804864017c71090929aec5`; current `build/native/atlas-native` differs (`eb7f854194fb901c248d098cf3c762d85f299190349a3d1f24dffb78c419158f`). `build/native-release/CMakeCache.txt` specifies Release, and the benchmark has its NDEBUG guard. The retained qualified copy therefore matches the Release artifact, not the Debug convenience binary. Hash identity proves these bytes match; the parent records the explicit copy provenance.

## Isolated proposed reference fix (not integrated)

```sh
python3 -B data-pilots/reliability/verify_candidate.py
```

`proposed-reference-fix.patch` is a small candidate for **later review**, not an applied production change. The runner copies only the current `animal_atlas` package into a new temporary directory, applies candidate text there, runs identical `acceptance_checks.py` against original and temporary package, saves `candidate-results.json`, then removes the temporary package. It enforces 30s per test-process budget; transport requests remain scripted and real urllib calls forbidden.

Observed: original reference fails all **four acceptance tests**; temporary patched reference passes **4/4**. The four gates are zero requests for pre-cancelled sharks, no publishable BirdWeather batch after cancellation during the final response, no ingestion when the adapter returns after stop, and an all-invalid exhausted page not ending the initial baseline or advancing its last-complete checkpoint. On the next valid exhausted poll, a new record remains baseline/non-pulsing and the baseline can complete. Original15-characterization results remain unchanged and the six counterexamples still describe production.

Candidate behavior intentionally keeps `Batch.complete`, `last_poll_complete` and `pagination_exhausted` as pagination results. It separately requires no reported invalid records before completing an initial baseline/advancing `last_complete_at_utc`; valid records in a partial/degraded poll are still retained. It does not reinterpret missing latest-fix subjects, invent historical completeness, delete old fixes or force acoustic identity semantics onto tracking. A single repeatedly invalid record could hold baseline open indefinitely: that is the conservative candidate policy, and integration must decide how to expose degraded/quarantined data without triggering a backlog as fresh sound.

Cancellation checks close demonstrated boundaries but do not interrupt an active blocking socket/read or make commit atomic with a concurrently changing stop flag. Native integration still needs operation generation and explicit publication ownership/linearization. The candidate intentionally leaves cancellation's current FetchError/backoff status treatment and stale retry-delay cleanup for a separate policy decision. These four fixture passes do not qualify a native transport or a real source. Production collector/store/adapters were not edited; no new production engine is introduced.
