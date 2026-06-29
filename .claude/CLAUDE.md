# polyfetch

## What this is
An experiment implementing the **same concurrent tool in four languages** — Go, C++,
Python, TypeScript — to learn and compare their **concurrency & parallelism models**
deeply. The goal is learning/comparison, not shipping a product. Medium scope: each
client is a focused, real little tool.

## The tool (frozen spec — identical behavior in all four clients)
A concurrent fetch + aggregate CLI:
1. Read `shared/targets.txt` (one target per line; URLs or local file paths).
2. **I/O stage:** fetch/read each target concurrently.
3. **CPU stage:** for each payload compute SHA-256, byte count, and `\n`-line count.
4. Aggregate: success/failure counts, total bytes.
5. Output a report sorted by `(size_bytes desc, target asc)`.
- `--concurrency K` flag caps simultaneous in-flight targets (bounded worker pool).
- A failed target is recorded as `status: error` and MUST NOT crash the run.

## Why this design
The I/O stage rewards concurrency in every language; the CPU stage (SHA-256) forces
the divergence: Go/C++ get true parallelism with no structural change, Python hits the
GIL (must use multiprocessing), TypeScript hits the event loop (must use worker_threads).
That contrast is the central lesson.

## Determinism rules (so all four produce identical output)
- Sort output by `(size_bytes desc, target asc)` — completion order is nondeterministic.
- Line count = count of `\n` bytes.
- SHA-256 is the cross-language correctness anchor; matching hashes prove identical reads.
- `fetch_ms` is timing data — log it, exclude it from equality checks.
- Pin the report format exactly (JSON lines + summary footer).

## Fairness rules (do not violate — the experiment depends on these)
- Clients couple to the world ONLY via `shared/targets.txt` and the server's HTTP API.
- NO shared code between clients. No shared utility lib, no generated bindings.
- Each client stays idiomatic with its NATIVE build tooling (go.mod, CMake, pyproject,
  package.json). Do not unify build systems across languages.
- Stdlib-only where feasible. Documented exceptions: C++ may use libcurl (no stdlib HTTP);
  Python may use aiohttp. Note such gaps as findings.

## The server (test fixture, not part of the experiment)
- Written in **Go** (boring, reliable, zero-dep, never the bottleneck).
- Runs in **Docker**, serves SYNTHETIC payloads generated on the fly (no stored files).
- Tunable knobs: `size`, `delay`, `status`, `count`.
- Same server for all four clients.

## Machine / measurement setup (16-core host)
- Server in Docker, cpuset-pinned to cores 0–3.
- Clients run NATIVELY (not containerized — avoids cgroup CPU caps), `taskset` to cores 4–15.
- Default workload: many medium payloads (low-MB each, hundreds–thousands of targets),
  NOT a few giant files (that measures memory bandwidth, not concurrency).
- Optional contrasting mode: a few giant files, to watch the bottleneck shift.
- Sweep `--concurrency` from 1 past 12; the throughput curve per language is the headline result.
- Discard first run (warmup/JIT), take median of repeats.

## Repo layout

```bash
polyfetch/
├── README.md              ← the experiment writeup + cross-language findings
├── SPEC.md                ← the frozen language-agnostic spec
├── docker-compose.yml     ← brings up the server
├── server/                ← the Go payload server (shared by all)
│   ├── main.go
│   └── Dockerfile
├── shared/                ← assets identical across all clients
│   ├── targets.txt        ← generated, points at localhost:8080
│   └── gen_targets.sh     ← regenerates targets.txt
├── scripts/
│   ├── run.sh             ← cpuset-pins + runs a client, sweeps concurrency
│   └── compare.sh         ← runs all four, collects results
├── results/               ← measurement output (the actual data you collect)
└── clients/
    ├── go/                ← fully standalone Go project (go.mod, etc.)
    ├── cpp/               ← fully standalone CMake project
    ├── python/            ← fully standalone (pyproject.toml / venv)
    └── typescript/        ← fully standalone (package.json, tsconfig)
```

## Two levels of findings
- Root `README.md`: cross-language comparative findings + concurrency-curve graphs.
- Each `clients/<lang>/README.md`: that language's journal — how bounded concurrency was
  expressed, whether I/O→CPU required a mechanism change, what the type system caught,
  how per-target errors propagated, LOC of concurrency logic, what fought vs. felt elegant.