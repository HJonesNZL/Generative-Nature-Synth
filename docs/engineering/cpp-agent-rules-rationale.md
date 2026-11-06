# Published foundations for the project agent rules

Research checked 6 November 2026. The owner requested strict, established guidance against unnecessary AI-written C++ complexity. `AGENTS.md` is the operative instruction file, including for the current session. This document explains its basis without expanding every future task's prompt.

There is no universal C++ style or prompt that guarantees clean code. This project selects a coherent subset of published guidance plus its existing owner/data/audio constraints. It does not claim all organisations agree on exception policy, templates, naming, formatting or test methodology. In particular, LLVM's project-specific exception/RTTI restrictions are not copied into all Animal Atlas code: worker code already uses exceptions; the realtime path must not.

## Agent prompting and instruction placement

OpenAI's official guidance for Astra recommends keeping repository instructions relevant and current, avoiding unnecessarily prescribed document-reading recipes, and giving clear boundaries for safe autonomous work. Its coding guidance uses focused changes and concrete validation rather than speculative complexity. Codex supports scoped `AGENTS.md` instructions and recommends concise review rules with formatting checks handled by tools.

Applied here: a compact persistent rule file; read relevant documents rather than the entire repository; state the task's observable outcome, boundaries and verification; use isolated local checks without repeated permission; report what is unproven. Do not request a prose chain of thought or add broad “be an expert” instructions as a substitute for an executable contract.

Useful task form:

```text
Outcome: <observable behaviour to add/fix>
Scope: <component and allowed files; preserve unrelated behaviour>
Constraints: <ownership/thread, data semantics, capacities, zero-cost requirements>
Evidence: <reproducer/fixture, relevant tests or benchmark and acceptance condition>
Deliver: <small complete patch plus results and remaining limits>
```

The task supplies only task-specific facts; durable C++/audio rules stay in `AGENTS.md`. This is a project prompt template informed by the sources, not a claim that its exact wording is an official required method. Judge it by code review, regressions and measured outcomes, not by instruction count.

Sources: [official Astra prompt guidance](https://developers.openai.com/blog/rethinking-skills-and-prompts-for-gpt-6-astra), [AGENTS.md instructions](https://learn.chatgpt.com/docs/agent-configuration/agents-md), [OpenAI coding prompt examples](https://developers.openai.com/cookbook/examples/gpt-5/gpt-5_prompting_guide).

## C++ design and ownership

The C++ Core Guidelines are the main language/design foundation: clear interfaces (I.1), one coherent operation per function (F.1–F.3), automatic resource lifetimes (R.1), scoped storage (R.5), explicit ownership (R.20–R.21), immutable values where appropriate, and no performance claims without evidence (Per.1–Per.6). These support small, understandable components instead of imagined extension frameworks.

Applied here: RAII for SQLite and other resources; unique ownership unless sharing is necessary; non-owning views for prepared buffers; relevant boundary validation; exceptions permitted away from callbacks; no invented hard limit on lines per function. Capacities reflect measured resource needs and correctness contracts, not a reward for writing fewer lines.

Source: [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines).

## Readability and automation

LLVM documents understandable names, deliberate library boundaries, early exits to reduce nesting, warning discipline, portability and avoiding unnecessary copies. Its formatting configuration is a recognised clang-format preset. This project adopts those applicable practices, with four-space indentation to retain the existing local convention. It does not import LLVM's entire internal-library style or reformat unrelated code.

Applied here: inspect changed code for redundant wrappers, accidental ownership/copies, duplicated state and silent errors; reject compiler warnings in project targets; use formatting automation when present. A formatter cannot prove good architecture. Neither clang-format nor clang-tidy was found on this session's PATH; no claim is made that existing files have passed those tools.

Sources: [LLVM coding standards](https://llvm.org/docs/CodingStandards.html), [clang-format configuration](https://clang.llvm.org/docs/ClangFormatStyleOptions.html).

## Realtime restrictions

PortAudio's callback guidance identifies allocation/deallocation, I/O, mutexes and OS-dependent operations as hazards because they lack bounded timing. Animal Atlas's existing brief also requires bounded transfer, no database/network work in the callback, safe stop/release behaviour and reproducible replay. The project's stricter action-capacity, lifetime and tail requirements are concrete applications of those audio constraints, not generic bans on ordinary worker-side C++.

Applied here: worker-side preparation; fixed callback work/storage; explicit overflow and plan retirement; no callback memory reclamation or blocking; measured deadlines under host/UI load. An allocation-free scheduling lookup is one useful check, not qualification of an entire audio instrument.

Source: [PortAudio callback guidance](https://portaudio.com/docs/v19-doxydocs/writing_a_callback.html), and the owner's Animal Atlas handoff requirements.

## Enforcing and revising the rules

The operative file is read and followed in this session. CMake project warnings and test/sanitizer commands enforce specific parts; review and evidence enforce the design/thread/data rules. Do not add arbitrary complexity or LOC quotas, blanket feature-framework bans without context, mandatory rewrites, or performance tricks without measurements. If a rule conflicts with a proven task need, explain the concrete trade-off and use the higher-priority owner instruction rather than silently bypassing it.
