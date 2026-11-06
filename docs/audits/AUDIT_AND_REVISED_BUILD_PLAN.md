# Animal Atlas — consolidated audit and revision record

Audit baseline 6 November 2026 UTC; final review 7 November NZDT. Three independent GPT-6 Astra reviews, requested by the owner, inform the replacement [build plan](../BUILD_PLAN.md). The work covers implementation/data, audio/DSP/CPU and the wider release lifecycle. The canonical plan is approximately 7,000 words across 21 areas, with nine gated milestones. This index records current status; the dated reports retain their original baseline findings.

## Beta 1 follow-up, 7 November

[M1 is complete for supported data/session formats](../milestones/M1_VERIFIED_PIECE.md): metadata and content verification, original provenance, 69-case shared event corpus, historical window policy and clean-checkout storage fixtures. The table below reflects this follow-up; individual audit reports retain their original evidence. Audio and release gates remain open.

## Decision

Continue with a **standalone-first instrument and shared portable C++ engine**, followed by a VST3 companion. React/TypeScript/Three.js in a native webview remains a reasonable UI recommendation. The owner has not yet chosen a JUCE licence/version or public distribution route. The current backend is a useful foundation; it is not an audible or release-qualified product.

Bring the first sound forward: repair verification/preservation, then render a frozen session through one bounded reference voice before building many effects or replacing the entire collector. Historical species/region/time selection and regional effects stay in the first product scope, with honest local-data capability before promising external archives. The full feature catalogue remains intact; phases distinguish what must work first.

## Independent reports

- [Implementation, session and data audit](astra-build-audit-2026-11-06.md): recovery, integrity, provenance, schema consistency, historical availability, storage testing and architecture.
- [Audio, realtime and CPU audit](astra-audio-cpu-audit-2026-11-06.md): overload determinism, musical actions, clocks/lifetimes, regional topology, DSP specifications and resource qualification.
- [Release quality and product readiness audit](astra-release-quality-audit-2026-11-06.md): security/privacy, rights, installation, devices/hosts, accessibility, backup, diagnostics and release engineering.

The reviewers read relevant implementation and specifications, used isolated fixtures and consulted primary documentation. They did not operate the owner's collector, library, audio device or DAW. No new live-provider traffic was needed. No P0 defect was established. P1 items below include both demonstrated defects and gates for unimplemented features; those are different evidence classes.

## Findings and remediation status

| Finding | Evidence | Current action/status |
|---|---|---|
| A1, P1: later `collect-once` acquisitions enter an abandoned recording | Actual CLI path with mocked polling/temp library | **Fixed.** Recover under exclusive writer ownership before acquisition; regression fails before fix and passes after. |
| A2, P1: native reader accepts altered frozen payload without verifying saved hash | Changed latitude accepted natively, rejected by Python | **Fixed in M1.** Canonical-byte native content verification and corruption fixtures pass. |
| A3, P2: import loses historical query/origin metadata | Export/import drops requested bounds and original policy | **Fixed in M1.** Bundle v2 preserves original context through repeated imports; separate local identity/time. |
| A4, P2: Python/C++ disagree on optional event semantics/types | Invented individual on station detection accepted only by Python | **Fixed in M1.** Both implementations pass 69 independently labelled semantic cases. |
| A5, P2: stored timing bounds are outside event hash | Bounds change shifts playback while event hash passes | **Fixed in M1.** Versioned envelope binds stored metadata; legacy metadata remains explicitly unverified. |
| A6, P2: replay responses omit capture-truncation warning | Saved cap flag absent from replay summary | **Fixed in M1 replay/API/export.** UI and audio render consumers must retain these flags in M3/M5. |
| A7, P2: normal native checks omit self-contained storage/CLI coverage | Real-library parity depends on owner `.state` | **Fixed in M1.** 18 disposable native storage/CLI tests include corruption, semantics and historical timing; no owner library required. |
| Audio, P2: comparator copies long IDs during sorting | 1,745,605 counted allocations for 50,000 synthetic events | **Fixed.** Compare time/sequence/ID without owning tuples; preparation-allocation regression added. |
| Audio, P1 integration: overload selection depends on block size | Four-event fixture with capacity two yields different surviving IDs | **Open M2 gate.** Freeze musical admission before callback; current data scheduler remains disclosed as incomplete for this purpose. |
| Audio, P1 integration: actions/voice releases/clock/publication/retirement absent | No audio engine exists yet | **Design gates, not bugs in existing DSP.** Executable lifecycle contracts, fixed capacities and stress tests before integration. |
| Audio, P1 integration: regional tails/topology need ownership | Regional DSP not implemented | **M5 gate.** Capped shared buses, uncertainty/overlap rules, bounded transitions and tail retirement. |
| R3, P2: client redirects retain bearer header across authorities | Redirect request constructed with synthetic token; no request sent | **Open small repair.** Deny local IPC redirects. Current legitimate API does not redirect; this is a hardening gap. |
| R3, P2: corrupt empty persisted token can be accepted | Method-level empty-server-token fixture | **Open small repair.** Validate/repair token under ownership. Healthy random-token auth was not bypassed. |
| R6, P2: only newest 100 sessions are discoverable | 101 temporary pieces; oldest readable by known ID | **Open library gate.** Stable bounded pagination/search and direct latest-eligible lookup. No deletion observed. |
| R1/R2, P1: distribution and source-use permissions unsettled | Source ledger licences unresolved; JUCE and signing conditions | **Release decision gates.** Versioned zero-cost licence/rights/distribution evidence. |
| R4/R5/R7–R12: bridge, backup, packaging, devices, accessibility, diagnostics and support unqualified | Missing implementation or acceptance evidence | **Release gates in plan.** No invented certification or unsupported compatibility claim. |

Repeated whole-plan rebuilding for paginated API responses is another demonstrated design cost. Prepare/cache bounded immutable results with cancellation and aggregate limits when that path is made production-native. It is not a reason to replace a working owner process during an audit.

## Completed verification and evidence boundary

- **Python:** 50 tests passed, including the new actual-CLI recovery regression; disposable fixtures and localhost test servers only.
- **Native:** 17 cases passed in Debug, Release and AddressSanitizer/UndefinedBehaviorSanitizer builds. Warnings-as-errors now covers all project/test targets. The new long-ID preparation regression failed before correction.
- **Synthetic worker preparation:** the original audit measured 53.1–64.3 ms and 1,745,605 allocations. The repaired implementation measured 7.3–11.9 ms and one counted allocation over three runs of the same 50,000-event harness, Apple Clang 17/arm64, `-O2`. Timing is workload/OS-dependent. This is not audio CPU or a DAW benchmark.
- **Earlier migration evidence:** 16 real-library comparison scenarios/18,992 events matched. These were not rerun as a substitute for new self-contained storage tests.
- **Not run/available:** clang-format/clang-tidy and Node/TypeScript tools were not on PATH. No JUCE/UI build, DSP listening, audio callback benchmark, DAW/device test, Windows/Linux qualification, installation or 72-hour soak has occurred.

Current repair evidence and retained audit probes are in [the audit evidence folder](../../evidence/audit-2026-11-06/). Baseline source hashes remain in [the original manifest](astra-build-audit-manifest-2026-11-06.json). Compiler/test flags and exact commands are recorded with the new evidence. None of these checks writes to the owner's database or interrupts a recording.

## Research-backed changes to the plan

The plan now specifies consistent SQLite backup/restore and library incarnation after sequence rewind; migration/retention and disk-failure policy; original selection/coverage preservation; one deterministic native action/audio engine; explicit callback lifetimes and clocks; bounded regional tails; numeric/latency/listening DSP gates; resource and graphics budgets; asynchronous bridge revisions/idempotency; private redacted diagnostics; real accessibility journeys; clean-machine packaging and installed-artifact tests; and device/host/upgrade support matrices.

JUCE must be version-pinned and evaluated for an accepted zero-cost route. Current official master points to JUCE 9; AGPL and conditional free Starter eligibility are choices to evaluate, not permission to assume a closed-source zero-cost release forever. Normal Developer ID distribution introduces the Apple membership decision; eligible fee waiver is not assumed. The current VST3 SDK is MIT. Source access remains distinct from collection/export/derived-output rights. Primary references and precise conditions are linked in the release audit and plan.

## Strict agent rules now active

[AGENTS.md](../../AGENTS.md) applies in this session and repository. [Rationale and researched prompt pattern](../engineering/cpp-agent-rules-rationale.md) document the basis: scoped outcome/constraints/evidence; minimal complete changes; RAII/clear ownership; direct single-purpose code; validation at boundaries; measured optimisation; no speculative framework; explicit realtime restrictions and meaningful verification.

Rules draw on [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines), [LLVM standards](https://llvm.org/docs/CodingStandards.html), [official Astra prompt guidance](https://developers.openai.com/blog/rethinking-skills-and-prompts-for-gpt-6-astra) and [callback guidance](https://portaudio.com/docs/v19-doxydocs/writing_a_callback.html). They select established practices rather than claiming unanimous agreement on every style or a prompt that guarantees quality. LLVM's blanket internal exception policy and arbitrary function/file line quotas were not adopted. `.clang-format` records the chosen preset; it was not run without its tool.

## Next milestone and owner decisions

M1 has closed native data verification, schema and preservation on the qualified fixture environment. M2 follows with one intentionally small audible slice. The detailed M0–M8 table and every exit criterion are in [the canonical plan](../BUILD_PLAN.md#19-gated-delivery-sequence).

The final Astra consistency pass also corrected three ambiguities: admission invariance holds for fixed playback settings across block partitions/offline execution rates; historical playback must version requested-window preservation versus trimming, including empty material; and M3 needs native portable-piece save/load even while library acquisition remains read-only until M4. Those are planned contracts, not features implemented by this audit.

Record the JUCE version/licence, legitimate zero-charge macOS distribution route, first supported platform/device/host matrix, permitted production sources/output uses and preservation/engine compatibility promise. These decisions do not block isolated fixtures, repairs or headless audio work, and this audit requests no purchase, publication or collector handover.
