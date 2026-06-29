---
name: experiment-invariants
description: Fairness and determinism rules for the polyfetch cross-language experiment. Auto-apply whenever creating or editing any client implementation (Go, C++, Python, TypeScript) or shared assets. These rules keep the four-language comparison valid; violating them invalidates the experiment.
when_to_use: Apply when writing or editing anything under clients/, the server, shared/targets.txt, or output/report code. Trigger on any work touching fetch, process, aggregate, report, concurrency, error handling, or dependencies in any of the four client languages.
user-invocable: false
paths: clients/**, shared/**, server/**
---

# polyfetch experiment invariants

These rules keep the four-language comparison valid. Follow them in all client work.

## Isolation
- Clients couple to the outside world ONLY via reading `shared/targets.txt` and calling the server's HTTP API. No other coupling.
- NEVER share code between clients. No shared utility library, no generated bindings, no copied helper modules.
- Each client uses its OWN native build tooling: Go = go.mod, C++ = CMake, Python = pyproject/venv, TypeScript = package.json. Do NOT unify build systems across languages.
- Write each client in that language's idioms. Do not impose one language's patterns on another.

## Dependencies
- Stdlib-only where feasible. Documented exceptions ONLY: C++ may use libcurl (no stdlib HTTP); Python may use aiohttp (stdlib async HTTP is painful).
- Before adding ANY other dependency, stop and flag it — it likely violates the spec. Note any stdlib gap as a finding in the client's README, do not silently work around it.

## Pipeline (identical behavior in all four clients)
- Stages: read targets -> fetch/read (I/O, concurrent) -> process (CPU: SHA-256 + byte count + `\n`-line count) -> aggregate -> report.
- `--concurrency K` flag caps simultaneous in-flight targets via a BOUNDED worker pool (not unbounded spawn). Express backpressure in the language's idiom.
- A failed target is recorded as `status: error` and MUST NOT crash the run. Continue processing remaining targets.

## Determinism (output must be byte-identical across languages)
- Sort output by (size_bytes DESC, target ASC) before emitting. Completion order is nondeterministic; never rely on it.
- Line count = count of `\n` bytes. Nothing else.
- SHA-256 is the cross-language correctness anchor; do not alter how bytes are read.
- `fetch_ms` is timing data: log it, but EXCLUDE it from any equality/correctness check.
- Report format is fixed: JSON lines per record, then a summary footer. Do not deviate per language.

## When in doubt
Preserve comparability. If a choice would make one client's behavior or output differ from the others for any reason other than the language itself, don't make it — surface the tension instead.