#!/usr/bin/env python3
"""polyfetch Python client.

Concurrency model: a pool of worker PROCESSES (multiprocessing via
ProcessPoolExecutor), each running the full fetch + process pipeline. Processes,
not threads, are what give Python true CPU parallelism: each process has its own
interpreter and its own GIL, so the hashing stage parallelizes across cores
without GIL contention. This is the structural change the experiment's thesis
predicts for Python — the direct analog of the TypeScript client reaching for
worker_threads to escape its single-threaded event loop. Go and C++ needed no
such change; one goroutine / OS thread handled both stages.

A thread-based variant (ThreadPoolExecutor) would be the key contrast: because
hashlib releases the GIL during large hashes, threads would parallelize the
SHA-256 work *partially*, while the pure-Python glue stays serialized — a more
nuanced picture than 'the GIL forbids CPU parallelism'. See process.py.
"""

import argparse
import sys
from concurrent.futures import ProcessPoolExecutor

from fetch import Record, init_worker, process_one
from report import write_report

# Exit codes, per the contract (§9):
#   0 — run completed (per-target errors do NOT change this)
#   1 — the run itself failed to complete
#   2 — invalid invocation (bad flag, unreadable targets file)
EXIT_RUN_FAIL = 1
EXIT_USAGE = 2


def parse_args(argv: list[str]) -> argparse.Namespace:
    # argparse exits with code 2 on its own for unknown flags, missing values,
    # and non-integer values for type=int — exactly the contract's usage-error
    # behavior. parser.error() (used for our extra checks) also exits 2.
    p = argparse.ArgumentParser(prog="polyfetch", add_help=True)
    p.add_argument("--concurrency", type=int, default=8,
                   help="max targets in flight simultaneously (>=1)")
    p.add_argument("--targets", default="shared/targets.txt",
                   help="path to targets file")
    p.add_argument("--base-url", dest="base_url", default="http://localhost:8080",
                   help="server base URL")
    p.add_argument("--timeout", dest="timeout_ms", type=int, default=30000,
                   help="per-target timeout in milliseconds")
    args = p.parse_args(argv)

    if args.concurrency < 1:
        p.error("--concurrency must be >= 1")
    if args.timeout_ms < 0:
        p.error("--timeout must be >= 0")
    return args


def read_targets(path: str) -> list[str] | None:
    """One target per line, skipping blank lines and lines beginning with '#'.
    The remainder of each line (including any '?size=...&delay=...') is preserved
    verbatim. Returns None if the file cannot be read."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
    except OSError:
        return None
    out: list[str] = []
    for raw in text.split("\n"):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        out.append(line)
    return out


def run(args: argparse.Namespace, targets: list[str]) -> list[Record]:
    """Concurrency core: K worker processes, each running the full fetch+process
    pipeline. ProcessPoolExecutor.map dispatches one target at a time
    (chunksize=1) so at most K targets are in flight — the contract's bound — and
    so that variable payload sizes are load-balanced rather than batched. map
    returns results in input order, so results[i] corresponds to targets[i]."""
    n = len(targets)
    results: list[Record] = [None] * n  # type: ignore[list-item]
    if n == 0:
        return results

    k = min(args.concurrency, n)
    timeout_s = args.timeout_ms / 1000.0

    with ProcessPoolExecutor(
        max_workers=k,
        initializer=init_worker,
        initargs=(args.base_url, timeout_s),
    ) as ex:
        for i, rec in enumerate(ex.map(process_one, targets, chunksize=1)):
            results[i] = rec

    return results


def main() -> None:
    args = parse_args(sys.argv[1:])

    targets = read_targets(args.targets)
    if targets is None:
        print(f"polyfetch: cannot read targets file: {args.targets}", file=sys.stderr)
        sys.exit(EXIT_USAGE)

    try:
        records = run(args, targets)
    except Exception as e:  # catastrophic run failure (pool could not run, etc.)
        print(f"polyfetch: run failed: {e}", file=sys.stderr)
        sys.exit(EXIT_RUN_FAIL)

    write_report(records, sys.stdout)
    # Normal interpreter exit flushes stdout; exit code 0.


if __name__ == "__main__":
    main()