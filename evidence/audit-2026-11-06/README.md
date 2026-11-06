# Audit evidence, 6 November 2026

These are isolated research probes, not the normal test suite. They use temporary libraries and synthetic values; no owner `.state`, source API, audio device or DAW is used. Some probes intentionally demonstrate still-open defects and therefore print a successful observation of incorrect behaviour. They are not acceptance tests that claim the whole product is healthy.

- `verification.json`: repaired-source hashes, test configurations and unrun checks.
- `session-probes.py` / `session-probes-after.json`: retained Astra session counterexamples, with only repository/binary paths made relative. The recovery result is now corrected; integrity/provenance counterexamples remain open.
- `release-probes.py` / `release-probes.json`: redirect-header construction, empty-token method and >100-piece listing probes. No requests are sent, even to loopback.
- `timeline-probe.cpp`: retained synthetic allocation/preparation and overload-partition harness.
- `core-before-fix.cpp`: exact audited comparator baseline, solely for reproducing the before/after measurement. It is not part of the product build.
- `timeline-before.txt`, `timeline-after-initial.txt`, `timeline-after.txt`: original baseline, first repaired measurement and later evidence rerun. Timing varies with OS/load; these are not product audio CPU results.

From the project root:

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native -j 4
ctest --test-dir build/native --output-on-failure
cmake -S . -B build/native-audit-release -DCMAKE_BUILD_TYPE=Release
cmake --build build/native-audit-release -j 4
ctest --test-dir build/native-audit-release --output-on-failure
cmake -S . -B build/native-audit-sanitized -DCMAKE_BUILD_TYPE=Debug -DATLAS_SANITIZERS=ON
cmake --build build/native-audit-sanitized -j 4
ctest --test-dir build/native-audit-sanitized --output-on-failure
python3 -B -m unittest discover -s tests -v
python3 -B evidence/audit-2026-11-06/session-probes.py
python3 -B evidence/audit-2026-11-06/release-probes.py
```

The Python suite uses disposable localhost servers; sandboxed environments may require network permission for those sockets. The probes do not need provider access. The native suite contains one CTest entry executing 17 cases, not 17 CTest executables. Warnings-as-errors is enabled by default in all three builds.

Synthetic measurements on this Mac used Apple Clang 17, arm64, `-O2`. IDs exceed small-string storage; event construction precedes the counted interval. Ordinary C++ `new`/`new[]` are counted. The same harness first demonstrates that the current bounded data lookup changes overload selection with block partitions; deterministic musical admission is still required before an audio engine.

```sh
clang++ -std=c++20 -O2 -Inative/include evidence/audit-2026-11-06/timeline-probe.cpp evidence/audit-2026-11-06/core-before-fix.cpp -o /private/tmp/atlas-timeline-before
/private/tmp/atlas-timeline-before
clang++ -std=c++20 -O2 -Inative/include evidence/audit-2026-11-06/timeline-probe.cpp native/src/core.cpp -o /private/tmp/atlas-timeline-after
/private/tmp/atlas-timeline-after
```

The original baseline manifest remains under `docs/audits/`. Do not overwrite these dated results with future runs; put new evidence in a separate dated/run directory.
