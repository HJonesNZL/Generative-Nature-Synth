# Data preparation review — offline only

Reviewer: GPT-6.1 SOL agent, 7 November 2026 (NZDT). Movebank and reliability received independent source/test review and reproduced checks. **GBIF is self-review:** this reviewer authored that pilot earlier. This is not human rights approval, a published-package review, a live-provider test or production integration qualification.

**Result: no new confirmed defect found in the pilot implementations reviewed.** Existing reference cancellation and stale-status defects are reproduced and remain unapplied in production. Individual-animal tracking is the prioritised expansion; the live listening/detection stream remains a distinct supported meaning. Generic GBIF occurrence work is correctly labelled optional/deferred.

## Inspected boundaries

Movebank: read `README.md`, `preflight.cpp`, `test_cells.cpp`, `test_preflight.py` and both synthetic CSVs. The one-shot worker caps each file at 4 MiB, 10,000 event rows, 64 columns and 4096 decoded bytes/cell. CSV decoding retains exact UTF-8 decoded values, doubled quotes, embedded commas/newlines and trailing empties; source files are never rewritten. Gregorian/UTC-millisecond syntax and finite geographic bounds are checked without rewriting the original strings. Duplicate IDs, sensor mismatch, unknown visibility/outlier flags, missing identity and unreviewed/private/generalized package declarations are excluded. Parsing/header/capacity failures return nonzero with no partial JSON success.

The output exposes technical candidate **row numbers**, not normalized animal events or coordinates. `import_allowed` is unconditionally false. Dataset DOI/licence/citation are explicitly unverified local declarations; fixture DOI/licence are clearly synthetic, not permission grants. Exact animal/tag strings are never converted into deployment IDs or universal identities. The unsupported animal/tag/deployment join, quality/uncertainty interpretation and package-specific privacy/rights work are permanent blockers in every result. A technical candidate is only a narrow syntax/profile result; it must not be treated as an approved fix or displayed track.

Reliability: read the contract, characterization/acceptance harnesses, synthetic fixtures, patch generator, proposed patch and recorded results. Every test uses disposable SQLite and scripted transport; urllib opening is explicitly forbidden. The candidate is applied only to a fresh temporary copy, with separate before/after test processes. The package import paths resolve the temporary patched package before the original fallback. The patch adds boundary cancellation checks and separates invalid-data baseline policy from pagination exhaustion; it does not implement socket interruption, atomic publication or native transport. Its conservative initial-baseline policy is described accurately, including the possibility that one repeated invalid record keeps baseline open indefinitely.

GBIF (self-review): planner returns requests only and caps work at three pages/60 scanned records/nine attempts/one in flight/60 seconds, with bounded 429 retries, explicit terminal states and cancellation/stale-response rejection. It requires a caller-supplied monotonic clock; transport/body-cap enforcement is documented as unimplemented. Synthetic source fields preserve original date representation, update/index times, licences, uncertainty and sensitivity flags. Every candidate is blocked from event v1 and has no display permission or invented individual/station/deployment ID. README correctly supersedes the earlier GBIF-first ranking and does not claim live API/taxon verification.

## Confirmed existing reference issues, not newly fixed production behavior

- **P2 cancellation boundaries:** pre-cancelled NZSharks makes two scripted bootstrap/config requests; cancelling during BirdWeather's final response still returns a complete batch; a collector adapter returning after stop can still publish one revision. Reproduced by the three cancellation cases in `reliability/acceptance_checks.py`; all fail against the unchanged original copy and pass against the temporary patched copy. No real request was sent.
- **P2 stale status:** characterization reproduces a 429 followed by success leaving persisted `retry_delay_seconds=120`. The worker's returned delay resets to zero, so this is stale health metadata, not proof of an additional 120-second wait. The proposed patch intentionally does not fix this issue.
- **Explicit policy gates, not unconditional provider bugs:** an all-invalid exhausted page currently completes initial baseline; out-of-window observations can be retained; missing latest-snapshot subjects do not delete prior history. The fourth candidate acceptance test demonstrates a proposed conservative invalid-baseline policy. Pagination exhaustion, usable data, observation freshness, coverage and ecological silence must stay separate. A disappearance must not manufacture an upstream tombstone.

The recorded six counterexamples remain visible. Fifteen passing characterization checks mean those observations were reproduced; they do not mean the existing implementation satisfies the proposed acceptance gates.

## Reproduced commands and outcomes

Commands below are the actual commands with canonical root and disposable directory factored as `$ROOT` and `$REVIEW_TMP` for readability. `$ROOT` was `/Users/henryjones/Documents/ChatGPT/Generative Nature Synth`. `$REVIEW_TMP` was a fresh Python `TemporaryDirectory(prefix="atlas-data-review-")`, removed after all checks. Before running the reliability commands, the reviewer copied `animal_atlas/` (excluding `__pycache__`) and `data-pilots/reliability/` into the same relative locations beneath that directory. Thus regenerated results/patch files stayed disposable and the retained pilot results were not edited. Python was run with `-B`; compiler was Apple clang via `c++`.

```sh
# Movebank strict optimized compile: exit 0
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -O2 "$ROOT/data-pilots/movebank/preflight.cpp" -o $REVIEW_TMP/preflight

# Movebank nine fixture tests: exit 0
/Applications/Xcode.app/Contents/Developer/usr/bin/python3 -B "$ROOT/data-pilots/movebank/test_preflight.py" $REVIEW_TMP/preflight -v

# Movebank exact-cell strict compile: exit 0
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -O2 "$ROOT/data-pilots/movebank/test_cells.cpp" -o $REVIEW_TMP/cells

# Movebank exact-cell regression: exit 0
$REVIEW_TMP/cells "$ROOT/data-pilots/movebank/fixtures/synthetic-events.csv" "$ROOT/data-pilots/movebank/fixtures/synthetic-package.csv"

# Reliability fifteen characterization tests: exit 0
/Applications/Xcode.app/Contents/Developer/usr/bin/python3 -B $REVIEW_TMP/data-pilots/reliability/check_reliability.py

# Reliability candidate before/after four acceptance gates: exit 0
/Applications/Xcode.app/Contents/Developer/usr/bin/python3 -B $REVIEW_TMP/data-pilots/reliability/verify_candidate.py

# GBIF strict planner compile: exit 0
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror "$ROOT/data-pilots/gbif/planner_tests.cpp" -o $REVIEW_TMP/gbif

# GBIF planner regression: exit 0
$REVIEW_TMP/gbif

# GBIF fixture contract: exit 0
/Applications/Xcode.app/Contents/Developer/usr/bin/python3 -B "$ROOT/data-pilots/gbif/check_fixtures.py" -v
```

All nine listed commands exited zero. Movebank: **9/9** Python test groups plus exact-cell regression passed under a strict optimized build. Reliability: **15/15** characterization tests, six retained counterexamples; original acceptance **4 failures**, isolated candidate **4/4 passes**. GBIF: planner checks and **4/4** fixture groups passed. Original acceptance failure is the expected before-fix result; `verify_candidate.py` itself exits zero only when that before/after condition is met. This review did not rerun Movebank/GBIF sanitizer variants; earlier author-reported sanitizer runs are not presented here as independently rerun evidence.

The reproduced reliability source hashes exactly matched retained `results.json`; regenerated candidate patch SHA-256 exactly matched the retained patch. All actual `animal_atlas/*.py` contents were hashed before/after and were **unchanged**. This review wrote only `data-pilots/REVIEW.md`; compilers and harness outputs used disposable storage. No owner library, collector process, credential, network/provider or paid dependency was accessed.

## Integration gates and limitations

No new source/import path is enabled by these artifacts. For prioritised individual tracking, select one approved small historical package, verify its DOI/content and rights/citation/privacy requirements, then preserve study/animal/tag/deployment reference joins, deployment bounds, original sensor quality/uncertainty, historical timestamps and gaps. Never infer identity from observer names, station IDs or tag reuse. Do not relabel a historical package as an active live feed.

Existing-source reliability work still needs a reviewed production/native integration decision: explicit cancellation generation and commit ownership, cancellable bounded transport/deadlines, retry-health cleanup, invalid-data baseline policy, withdrawal/correction handling and honest per-source freshness/coverage status. The proposed reference patch is not applied, native-qualified, concurrency-qualified or an end-to-end deadline guarantee. Optional GBIF work must remain deferred unless separately chosen, with event-schema/date/privacy/licence gates resolved before any activation.

No real package, current provider availability, live freshness, real animal identity/deployment join, intended-use licence compatibility, sensitive-coordinate release, long-duration collector or ecological validity was qualified. No claim of distribution rights or completeness follows from public accessibility or a passing synthetic fixture.

Reviewed pilot manifest SHA-256 (sorted relative paths and per-file SHA-256, excluding this report): `6631868d217b9d8ca4a1d0d850aa5cc4caec4763345477dddc2deffdf75ab7dd`.
