# GBIF occurrence pilot — OPTIONAL / DEFERRED

Owner steering, 7 November 2026: keep the existing live listening/detection stream; prioritise **individual-animal tracking** for other streams, with the Movebank workstream ahead of this generic occurrence pilot. Nothing here is an enabled source, collector, import, track, or next-source recommendation. GBIF occurrence records must never acquire fabricated individual, deployment or station IDs.

This directory is a small offline contract experiment for possible later integration. It does not change production, native CMake, the database or the UI. All response/candidate examples are **synthetic**; their keys, dataset, coordinates and timestamps are invented for tests. Do not follow synthetic occurrence URLs or cite these fixtures as wildlife evidence. Public documentation was read; no occurrence search or record/dataset harvest was performed. An attempted read-only taxon API lookup was inaccessible through the research tool; no taxon JSON response was verified.

## Implemented offline

`planner.hpp` is standalone portable C++20 worker-side request planning. Input is caller-supplied nondecreasing monotonic milliseconds and validated page/HTTP outcomes; output is an optional request and explicit state. One owner; no thread sharing. It sends no HTTP, waits on no clock and imports no records. The caller must stop an in-flight HTTP operation on cancel/deadline; late completions are rejected. `planner_tests.cpp` checks exact URLs, page and total budgets, one in flight, empty/error separation, stale completions, cancellation, retry ceilings and clock overflow. Tests use exceptions/checks in Release, not disabled assertions.

Fixed proposed query: Aves `taxonKey=212`, New Zealand `country=NZ`, occurrence year 2020, coordinates present, no reported geospatial issue, occurrence status PRESENT. `year=2020` is a filter, **not** a precise instant or proof that the complete occurrence interval is inside 2020. Official [Aves page](https://www.gbif.org/species/212) currently resolves to the newer taxon presentation; verify the API taxonomy/key before any opt-in execution. No taxon or dataset is approved for activation here.

Budgets are pilot choices: 20 records/page, three successful pages, at most 60 scanned records, at most nine HTTP attempts including retries, one in flight, at least one second after a page, 60 seconds total. A short nonfinal page still advances by the requested limit; nonfinal empty pages fail closed. Successful `endOfRecords` means complete; budget exhaustion means truncated, never “global total”. Repeated keys still consume the scanned budget. Search results are mutable, not a snapshot; eventual integration must deduplicate by GBIF key and retain request/response provenance and server count separately from unique imported count.

Only 429 is retried, at most twice per page. Backoff is two then four seconds, increased to a longer numeric `Retry-After`. HTTP-date, malformed/overflowing Retry-After, exhausted retries or delays that reach the overall deadline produce `rate_limited`, with no automatic resumption. A future HTTP adapter must strip header whitespace, validate exactly one Retry-After, implement its transport timeout/body cap, and surface terminal failures; other network/HTTP/parse failures call `fail`. No automatic retry on missing/error data and no spinning. Caller must not reset/recreate the planner to evade budgets. [GBIF guidance](https://techdocs.gbif.org/en/openapi/) documents load-dependent 429 responses and offset/limit paging; these pilot limits do not promise a supported request rate.

## Response → candidate contract

`fixtures/synthetic-mapping-cases.json` contains six manually labelled examples: day, month, year with sensitivity flags, date interval with NC licence, timezone-bearing timestamp with unconfirmed original precision, and missing date/coordinates/licence. `check_fixtures.py` validates preservation and adversarial mutations; it is a test-only contract checker, **not** an API JSON parser or second production engine.

Every selected response field is copied unchanged into `candidate.source_fields.<sameName>`. Missing stays missing, null stays null; no fallback fills an unknown value. Original numeric `key` is also decimal text in `provider_event_key`; URL is `https://www.gbif.org/occurrence/<key>`. The preserved groups are:

| Response fields | Candidate meaning |
| --- | --- |
| `key`, `occurrenceID`, `datasetKey`, `publishingOrgKey`, `datasetName`, `references` | GBIF, publisher and dataset identity; references remain an opaque source link |
| `license`, `rightsHolder` | Licence and attribution inputs; never automatic reuse approval |
| `taxonKey`, `classKey`, `kingdom`, `class`, `scientificName` | Interpreted taxonomy, not individual identity |
| `basisOfRecord`, `occurrenceStatus`, `countryCode`, `issues` | Record/category/filter/quality context |
| `eventDate`, `year`, `month`, `day` | Reported interpreted event date and components; preserve strings/ranges |
| `modified` | Source modification, never observation time |
| `lastCrawled`, `lastParsed`, `lastInterpreted` | GBIF processing times, never animal freshness |
| `decimalLatitude`, `decimalLongitude`, `geodeticDatum`, `coordinateUncertaintyInMeters` | Published point, datum and uncertainty; absence is unknown |
| `dataGeneralizations`, `informationWithheld` | Sensitivity information retained verbatim; never reverse-engineer hidden coordinates/dates |

These semantics follow the official [GBIF occurrence model](https://gbif.github.io/gbif-api/apidocs/org/gbif/api/model/occurrence/Occurrence.html). [Darwin Core eventDate](https://dwc.tdwg.org/list/#dwc_eventDate) permits dates/intervals; formatted midnight does not establish original measurement precision. `date_representation` describes syntax only; even a timestamp remains `timestamp_unconfirmed`. All fixtures leave `observed_at_utc`, ingestion times and ecological IDs null. No timestamp is synthesised from a year/month/day, modification/index date or download time. Future ingestion may separately stamp actual local receipt time, labelled as receipt.

All candidates carry `integration_status=blocked_event_v1_schema`, `display_permission=none`, `synthetic_fixture=true`, `provenance_mode=demo`. They are intentionally invalid as stored event v1. Real archive ingress would be explicitly imported, never demo or live merely because the API was queried now. The current schema requires a precise observation timestamp and has no generic occurrence kind or original date interval/privacy representation; do not disguise these points as acoustic stations, GPS fixes or individual trajectories.

Licence gates are conservative fixture policy: recognised CC0/CC BY still need dataset/citation review; CC BY-NC is excluded from any distributable pilot; missing/unrecognised licence is unresolved. Media is not selected or downloaded. [GBIF licence guidance](https://ipt.gbif.org/manual/en/ipt/latest/applying-license) distinguishes dataset/record obligations. Actual dataset citation, licence consistency, publisher rights and intended-use compatibility remain unverified. Sensitivity is flagged when either sensitivity field is populated; absent fields remain **unassessed**, never a claim of safety. The [GBIF sensitivity report](https://docs.gbif.org/sensitive-species-data-generalization/en/) describes inconsistent generalisation across publishers; review the selected dataset before exposing any point.

## Not implemented / integration gates

No HTTP adapter, JSON ingress parser, cache, model migration, UI, collector, scheduler, update/deletion refresh or cross-source reconciliation exists here. Future ingress must enforce a 512 KiB decompressed body/page cap and a 16 KiB selected-record cap before persistence, reject invalid types/nonfinite coordinates/uncertainty and preserve unknown source metadata through an explicit versioned policy. These body limits are designed requirements, not planner-enforced protections. Unknown provider fields must not be silently discarded by a future adapter; the fixture checker recognises only its documented synthetic subset.

Before activation: reverify API parameters and taxon identity, inspect one approved dataset's metadata/citation/privacy and licence, implement bounded transport/cancellation, original-date semantics and schema support, then cache correction/deletion and duplicate policy (including republished iNaturalist IDs). A 404 must not alone establish an ecological deletion. Search count/index availability does not imply complete coverage. Nothing authorises a bulk download, credentials, paid service, owner-state experiment or automatic polling.

## Reproduce offline checks

From the canonical project root, create binaries only in disposable storage:

```sh
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror data-pilots/gbif/planner_tests.cpp -o /tmp/gbif-pilot-tests
/tmp/gbif-pilot-tests
python3 -B data-pilots/gbif/check_fixtures.py -v
```

Verification: Debug and Release warnings-as-errors planner checks plus ASan/UBSan pass; four fixture test groups pass. This is **implemented offline / not live verified**. No native application or listening/device test is relevant to this deferred occurrence contract.
