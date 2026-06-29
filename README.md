# polyfetch

A cross-language experiment: build the **same concurrent fetch-and-aggregate tool** in
**Go, C++, Python, and TypeScript**, then compare how each language expresses
**concurrency and parallelism**. The goal is learning and comparison, not shipping a
product. Each client is a focused, real little tool that must produce **byte-identical
output** against a shared local server.

This README is the frozen contract every client must satisfy.

---

## 1. What we are measuring

Each client runs a two-stage pipeline against a local payload server:

- An **I/O-bound stage** (fetching payloads) that rewards concurrency in *every* language,
  including the single-threaded ones, because the work is waiting on the network.
- A **CPU-bound stage** (hashing each payload) where the languages diverge: Go and C++ get
  true multi-core parallelism, while Python (GIL) and TypeScript (event loop) must reach for
  a *different mechanism* to parallelize CPU work.

The central observation: **Go and C++ need no structural change between the two stages;
Python and TypeScript do.** The shape of the throughput-vs-concurrency curve per language is
the headline result.

This is the *only* axis the experiment optimizes for. The interface and output are pinned
exactly so that the *only* thing that varies between clients is how each language handles
concurrency.

---

## 2. What is fixed vs. what is free

| Fixed (pinned by this contract) | Free (the thing under study) |
|---|---|
| The CLI flags and their semantics | How the bounded worker pool is implemented |
| The server API the clients call | How the I/O stage is made concurrent |
| The per-record JSON schema and key order | How the CPU stage is parallelized |
| Number/string formatting in output | Native build tooling and project ergonomics |
| Output sort order | Error-propagation idiom (within the rules below) |
| Exit codes | Internal data structures |

**Do not** pin or unify the concurrency implementation across languages — that divergence
is the experiment. **Do** pin everything that affects whether two clients produce identical
bytes.

---

## 3. The fairness rules (non-negotiable)

These keep the comparison valid. They are also encoded in
`.claude/skills/experiment-invariants/`.

1. Clients couple to the outside world **only** by (a) reading `shared/targets.txt` and
   (b) calling the server's HTTP API. No other coupling.
2. **No shared code between clients.** No shared utility library, no generated bindings, no
   copied helpers.
3. Each client uses its **own native build tooling** (`go.mod`, CMake, `pyproject.toml`,
   `package.json`). Do not unify build systems.
4. Each client is written in **that language's idioms**. Do not impose one language's
   patterns on another.
5. **Standard library only** where feasible. The *only* permitted dependency exceptions:
   - **C++** may use **libcurl** (no stdlib HTTP).
   - **Python** may use **aiohttp** (stdlib async HTTP is painful).
   Any other dependency is a spec violation. A stdlib gap is itself a finding — record it,
   don't silently work around it.

---

## 4. The pipeline (identical behavior in all four clients)

For each target in `shared/targets.txt`:

1. **Fetch** the target's bytes over HTTP (I/O-bound; perform concurrently).
2. **Process** the bytes (CPU-bound; parallelize):
   - `sha256` — lowercase hex SHA-256 of the raw bytes.
   - `size_bytes` — number of bytes received.
   - `line_count` — count of `0x0A` (`\n`) bytes. Nothing else counts as a line.
3. Produce one **record** (schema in §7).

Then **aggregate** all records and emit a **report** (§7), sorted per §8.

A target that fails (network error, non-2xx status, timeout) is recorded with
`status: "error"` and **must not crash the run**. Processing of the remaining targets
continues.

`line_count` is the raw count of `\n` bytes. A file ending without a trailing newline
therefore has `line_count` = (visible lines − 1). This is intentional and unambiguous.

---

## 5. Command-line interface

Every client exposes the **same** CLI:

```
polyfetch [--concurrency K] [--targets PATH] [--timeout MS]
```

| Flag | Type | Default | Meaning |
|---|---|---|---|
| `--concurrency` | int ≥ 1 | `8` | Max targets in flight simultaneously (bounded pool). |
| `--targets` | path | `shared/targets.txt` | Input file, one full URL per line. |
| `--timeout` | int (ms) | `30000` | Per-target timeout. Exceeding it → `status: "error"`. |

- `--concurrency` is the knob you sweep (1 → past your client core count) to produce the
  result curve. It must cap *simultaneous in-flight targets* via a **bounded** worker pool,
  not an unbounded spawn-everything approach. **How** the bound is enforced is free and
  language-idiomatic (buffered channel/semaphore, thread pool size, `asyncio.Semaphore`,
  a promise-pool, etc.).
- Invalid flag values (e.g. `--concurrency 0`) → exit code `2`, message to stderr.
- An unparseable URL in `targets.txt` is an invocation error → exit code `2`, message to
  stderr, no records emitted. (Distinguishes malformed input from runtime fetch failures.)

---

## 6. Targets file

`shared/targets.txt` contains **one full URL per line**. Blank lines and lines beginning
with `#` are ignored. The client passes each URL through to the HTTP layer unchanged.

Example `shared/targets.txt`:

```
# medium-payload workload
http://localhost:8080/payload/0001?size=1048576
http://localhost:8080/payload/0002?size=1048576&delay=50
http://localhost:8080/payload/0003?size=524288
http://localhost:8080/payload/0004?status=500
```

Full URLs in the file give you per-target heterogeneity in one place: mix sizes, inject
slow targets among fast ones, force specific failures by URL. The workload is **fully
described by `targets.txt`** — to reproduce a run, share that one file. To repoint at a
different host or port, regenerate `targets.txt` via `gen_targets.sh`.

The **server** controls payload characteristics via the query params on each URL:

| Param | Meaning | Determinism |
|---|---|---|
| `size` | payload size in bytes (e.g. `?size=1048576`) | identical bytes per (id,size) |
| `delay` | ms the server waits before responding | simulates slow I/O |
| `status` | force an HTTP status (e.g. `?status=500`) | exercises error handling |

Payloads are **generated deterministically from the target path + params** (seeded PRNG),
so every client and every run receives byte-identical data for the same URL without any
file or database on the server. The matching `sha256` is therefore stable and acts as the
cross-language correctness anchor.

---

## 7. Output contract (pinned to the byte)

Output goes to **stdout**. It is two parts: one JSON object per line for each record, then a
single summary line. This is JSON Lines, not a JSON array.

### 7.1 Per-record line

Exactly these keys, in **this order**, one compact JSON object per line (no spaces after
`:` or `,` — i.e. the most compact standard form; see §7.4):

```json
{"target":"http://localhost:8080/payload/0001?size=1048576","status":"ok","size_bytes":1048576,"line_count":4096,"sha256":"<64 hex chars>","fetch_ms":12}
```

| Key | Type | Notes |
|---|---|---|
| `target` | string | the full URL from `targets.txt`, verbatim |
| `status` | string | `"ok"` or `"error"` |
| `size_bytes` | integer | bytes received; `0` on error |
| `line_count` | integer | count of `\n` bytes; `0` on error |
| `sha256` | string | 64 lowercase hex chars; empty string `""` on error |
| `fetch_ms` | integer | wall-clock ms for the fetch; **excluded from equality checks** |
| `error` | string | **present only when** `status` is `"error"`; short reason |

### 7.2 Summary line

After all record lines, exactly one summary object:

```json
{"summary":true,"total":3,"ok":3,"errors":0,"total_bytes":3145728}
```

| Key | Type | Notes |
|---|---|---|
| `summary` | bool | always `true`; marks this as the footer |
| `total` | integer | number of targets attempted |
| `ok` | integer | count with `status:"ok"` |
| `errors` | integer | count with `status:"error"` |
| `total_bytes` | integer | sum of `size_bytes` across `ok` records |

### 7.3 Determinism / equality

Two clients' outputs are considered **equal** when, after removing the `fetch_ms` field from
every record line, the byte streams are identical. `fetch_ms` is the *only* field allowed to
differ. The `sha256` values matching across clients proves all clients read identical bytes;
a mismatch is a real bug in that client's I/O, not noise.

### 7.4 Formatting rules (so bytes match)

- Records sorted per §8 **before** printing.
- Compact JSON: no whitespace except the single `\n` terminating each line. Key order as
  listed above (do not sort keys alphabetically).
- Integers as bare digits, no `+`, no leading zeros, no thousands separators.
- `sha256` lowercase hex, exactly 64 chars when `ok`.
- UTF-8, Unix line endings (`\n`), single trailing newline after the summary line.

---

## 8. Sort order

Records are sorted **before** output by:

1. `size_bytes` **descending**, then
2. `target` **ascending** (lexicographic byte order on the full URL string).

This normalizes the nondeterministic completion order of concurrent fetches into a stable,
identical-across-clients ordering. Error records (`size_bytes` = 0) therefore sort last,
ordered among themselves by `target` ascending.

The summary line always comes last, after all sorted records.

---

## 9. Exit codes

| Code | Meaning |
|---|---|
| `0` | Run completed. **Per-target errors do not change this.** A run where some targets failed but the pipeline ran to completion still exits `0`. |
| `2` | Invalid invocation (bad flag, missing/unreadable `targets.txt`). |
| `1` | The run itself failed to complete (e.g. cannot bind resources, unexpected fatal error). |

The distinction is deliberate: a `500` from a target is *expected, handled data* (exit `0`,
recorded as an error record), whereas being unable to run at all is a process failure
(exit `1`). This keeps "the tool worked and reported failures" separate from "the tool
broke."

---

## 10. The server (test fixture — not part of the experiment)

Written in **Go** (boring, reliable, zero-dependency, never the bottleneck). Runs in
**Docker**. Serves **synthetic payloads generated on the fly** from a seed derived from the
target ID — no files, no database. Same server binary serves all four clients, so it is the
single source of truth for payload bytes.

Knobs (§6): `size`, `delay`, `status`. Determinism comes from seeding a PRNG with a stable
hash of the request, so identical requests always yield identical bytes, across restarts and
across clients.

The server is documented and built separately; this README governs the **clients'** contract
with it.

---

## 11. Repository layout

```
polyfetch/
├── README.md            # this file — overview, contract, findings
├── CLAUDE.md            # context for Claude Code
├── docker-compose.yml   # brings up the server
├── .claude/             # project settings + experiment-invariants skill
├── server/              # Go payload server + Dockerfile
├── shared/
│   ├── targets.txt      # the workload — identical for all clients
│   └── gen_targets.sh   # regenerates targets.txt
├── scripts/
│   ├── run.sh           # cpuset-pin a client, sweep --concurrency
│   └── compare.sh       # run all four, diff outputs (ignoring fetch_ms)
├── results/             # collected measurement data + curves
└── clients/
    ├── go/              # standalone, idiomatic Go project
    ├── cpp/             # standalone CMake project
    ├── python/          # standalone pyproject/venv project
    └── typescript/      # standalone package.json project
```

`SPEC.md` is folded into this README for now. Split it out once the contract (§4–§9)
stabilizes and you want it frozen and citable. The split seam is clean: §4–§9 lift out as
`SPEC.md`, leaving overview + findings here.

---

## 12. Running the experiment (16-core host)

Bandwidth-free, reproducible, engineered so only the language varies.

1. **Start the server**, pinned to a few cores so it can't steal from the clients:

   ```bash
   docker compose up -d            # server on localhost:8080
   # server container cpuset-pinned to cores 0-3 (see docker-compose.yml)
   ```

2. **Generate the workload** (many medium payloads is the default — that exercises the
   worker-pool logic; a "few giant files" mode is a separate contrasting run):

   ```bash
   ./shared/gen_targets.sh         # writes shared/targets.txt
   ```

3. **Run a client**, pinned to the remaining cores, sweeping concurrency:

   ```bash
   taskset -c 4-15 clients/go/polyfetch --concurrency 1
   taskset -c 4-15 clients/go/polyfetch --concurrency 4
   taskset -c 4-15 clients/go/polyfetch --concurrency 12
   # ... repeat per client, per concurrency level
   ```

4. **Verify correctness** — all four outputs must match (ignoring `fetch_ms`):

   ```bash
   ./scripts/compare.sh            # diffs the four clients' output
   ```

**Measurement hygiene:** discard the first run of each client (warmup / JIT — matters
especially for Python and TypeScript), then take the **median** of several repeats. Keep
client and server on disjoint core sets so server-side payload generation never contaminates
client measurements.

---

## 13. Findings (filled in as you go)

The payoff lives here. Each client also keeps its own journal in
`clients/<lang>/README.md`; this section is the **cross-language synthesis**.

### 13.1 Concurrency curves

> Throughput (targets/sec) vs. `--concurrency`, one line per language. Expected shape: Go
> and C++ keep climbing on the CPU stage until cores saturate; Python and TypeScript plateau
> earlier on CPU-bound work unless multiprocessing / worker_threads are used. _(insert chart)_

### 13.2 Per-language observations

For each of Go, C++, Python, TypeScript, record:

- How "bounded concurrency of K" was expressed, and how natural it felt.
- **Did the mechanism change between the I/O stage and the CPU stage?** (Expected: no for
  Go/C++, yes for Python/TypeScript — note exactly what changed and why.)
- What the type system caught before runtime.
- How per-target errors propagated without killing the run.
- Lines of code for the concurrency logic specifically.
- What fought you; what felt elegant.
- Any stdlib gap that forced an exception (libcurl, aiohttp) — and how it felt.

### 13.3 Cross-cutting takeaways

> The comparative conclusions — where the four languages' concurrency philosophies genuinely
> differ, and where the differences turned out to be smaller than expected. _(write last)_

---

## 14. Locked decisions

These were the deliberate choices made when locking this contract. Recorded here so the
*reasoning* behind the contract isn't lost — and so any future revisit knows which sections
are intentional, not accidental.

| # | Section | Choice | Rejected alternative |
|---|---|---|---|
| 1 | §4 | `line_count` = raw count of `\n` bytes | "Human" line count including final unterminated line |
| 2 | §5 | Default `--concurrency` = `8` | Other defaults (cosmetic) |
| 3 | §6 | Target IDs expanded to `<base-url>/payload/<id>` | Full URLs per line |
| 4 | §6 | Workload params inline in `targets.txt` | Separate workload-profile file |
| 5 | §7 | JSON Lines records + compact summary footer | Single pretty-printed JSON document |
| 6 | §9 | Exit `0` even when some targets error | Exit nonzero if any target errored |
| 7 | §11 | `SPEC.md` folded into this README for now | Separate `SPEC.md` from the start |

If you ever want to revisit a choice, the rationale lives in the conversation that
produced this contract; the table above is the audit trail.