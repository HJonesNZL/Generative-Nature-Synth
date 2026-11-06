# Movebank historical tagged-animal preflight pilot

Implemented 7 November 2026 as an isolated **contract validator**, not an importer, data approval or live source. No study has been selected or approved. All included observations, IDs, package declarations and locations are synthetic. Only this directory is owned by the pilot; production C++, canonical events, databases, collectors and UI are unchanged.

## Behaviour and boundaries

An off-thread, one-shot C++20 CLI reads a local event CSV and one local package-declaration CSV. It returns JSON technical candidate record numbers, exclusion reasons, unverified package declarations and permanent integration blockers. A technical candidate means only “passed this narrow syntax/profile check.” `import_allowed` is always false, even for all-valid input. No event, animal, deployment, track, canonical timestamp or position is generated. No network, SQLite, accounts or external library is used. Input files are read-only; row coordinates and animal/tag IDs are not copied to the report. Source bytes remain the authority.

Parsing keeps exact decoded UTF-8 cell values, including quoted commas, doubled quotes, multiline text and trailing empty cells. It does not trim IDs, coerce event IDs to numbers, round coordinates, rewrite timestamps or invent defaults. CSV quoting is decoded; the original source must be retained separately for byte identity. UTF-8/control validation, duplicate/empty headers, ragged records, malformed quotes and capacity excess fail the whole invocation without a partial success report. Limits: 4 MiB per file, 10,000 event records plus header, 64 columns and 4,096 decoded bytes per cell. Exit 0 means the report completed, including when every row is excluded; exit 1 means file/contract failure. Allocation and exceptions are confined to this worker CLI.

## Deliberately narrow profile

Required event columns: `event-id`, `timestamp`, `location-long`, `location-lat`, `individual-local-identifier`, `tag-local-identifier`, `sensor-type`, `study-name`, `visible`, `manually-marked-outlier`. Official terms and their CSV-style hyphenated names are documented by the [Movebank Attribute Dictionary](https://www.movebank.org/cms/movebank-content/movebank-attribute-dictionary). Coordinates describe sensor estimates in WGS84 decimal degrees. This pilot accepts only `gps`; it does not classify receiver/acoustic, Argos or other sensor positions as equivalent GPS fixes. Accepted timestamp spelling is exactly `yyyy-MM-dd HH:mm:ss.SSS`, with valid Gregorian dates and UTC interpretation. Coarser dates, alternate offsets/ISO spelling and greater precision are excluded, not silently coerced. This says nothing about actual sensor accuracy.

Visibility must explicitly be `true`/`TRUE`, and manually marked outlier must explicitly be `false`/`FALSE`. Missing flags are excluded. Movebank's `visible` field summarizes several filters and manual overrides; the extra manual-outlier requirement is a conservative pilot restriction, not a replacement for Movebank's visibility logic. Other columns are preserved opaquely in memory but are **not semantically qualified**, including additional quality, uncertainty and privacy attributes. Candidate status must not be used to bypass them. Exact position/time privacy must be reviewed before supplying the declaration below; the tool cannot detect a false declaration.

The separate package CSV is a **local pilot declaration format**, not a claimed Movebank export schema. It requires exactly one data row and seven columns: `package-doi`, `study-name`, `license-uri`, `citation`, `coordinate-units`, `download-access`, `privacy-review`. Names/attribution are retained verbatim in the JSON report. DOI/licence/citation must be nonempty but are not resolved, authenticated or approved. Required declarations are `decimal-degrees-WGS84`, `public-download`, `public-exact-reviewed`; private, generalized or unreviewed status excludes every row. `visible=true` is never treated as download permission or privacy clearance. Fixture values deliberately say `synthetic:not-a-real-doi` and `synthetic:no-license-approved`.

## Why canonical import is blocked

Movebank associates tag events with animals through deployments; one animal can have multiple deployments. The [official data model](https://www.movebank.org/cms/movebank-content/mb-data-model) distinguishes events from animal/tag/deployment reference data. This event-only validator does not implement that reference join or verify deployment bounds. Missing deployment identity is never filled from tag ID, animal ID or time adjacency. A later approved package integration must preserve exact study-scoped animal/tag/deployment identities and their provenance, rather than connect unrelated tag reuse periods.

The [official API documentation](https://github.com/movebank/movebank-api-doc/blob/master/movebank-api.md) warns that internal identifiers can change and local identifiers need study context. Repository-specific README/reference mappings, quality/uncertainty fields, DOI provenance, metadata integrity, duplicate/correction policy and any generalized/withheld fields remain unimplemented. There is no live freshness, interpolation or track continuity claim. The existing listening/detection source remains separate from this future individual-location stream.

Licence selection is a user/review gate: [Movebank policy](https://www.movebank.org/cms/movebank-content/data-policy) provides study-specific terms and CC0/BY/BY-NC choices; access alone is not reuse approval. No licence is inferred from platform membership or map visibility. No private or online study data were harvested. Before integration, select one small published package and review its DOI, exact licence/version, citation, intended use, sensitivity, reference joins and package README. Current declarations only exercise the boundary with synthetic examples.

## Reproduce offline

From the canonical repository, using a C++20 compiler:

```sh
clang++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -O2 data-pilots/movebank/preflight.cpp -o /tmp/atlas-movebank-preflight
/tmp/atlas-movebank-preflight data-pilots/movebank/fixtures/synthetic-events.csv data-pilots/movebank/fixtures/synthetic-package.csv
python3 -B data-pilots/movebank/test_preflight.py /tmp/atlas-movebank-preflight -v
clang++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -O2 data-pilots/movebank/test_cells.cpp -o /tmp/atlas-movebank-cells
/tmp/atlas-movebank-cells data-pilots/movebank/fixtures/synthetic-events.csv data-pilots/movebank/fixtures/synthetic-package.csv
```

Verification on Apple clang 17.0.0: strict optimized build, nine independent Python fixture tests, and direct C++ exact-cell regression. ASan+UBSan build uses `-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer` and runs the same nine tests. Fixtures cover quoted/UTF-8/multiline/trailing-empty parsing, missing IDs, duplicate IDs, coarse/impossible/offset timestamps, invalid coordinates, sensor mismatch, outlier/visible flags, private/generalized/unreviewed declarations, units/licence gaps, malformed CSV/UTF-8 and file/row/column/cell capacity. No actual published package, owner library, live provider, deployment join or production integration has been qualified.
