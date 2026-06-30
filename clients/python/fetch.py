"""I/O-bound stage: fetch a target over HTTP and stream it through the CPU
stage. Also defines the worker-process entry points used by main.py's process
pool. These live in this importable module (not __main__) so they pickle cleanly
across the process boundary under both fork and spawn start methods."""

import http.client
import time
from dataclasses import dataclass
from urllib.parse import urlparse

from process import StreamProcessor

_CHUNK = 64 * 1024


@dataclass
class Record:
    """One output record. The field order here is the JSON key order the
    contract pins exactly (target, status, size_bytes, line_count, sha256,
    fetch_ms, error). report.py emits `error` only when status == "error"."""

    target: str
    status: str
    size_bytes: int
    line_count: int
    sha256: str
    fetch_ms: int
    error: str = ""


def _elapsed_ms(start: float) -> int:
    return int(round((time.perf_counter() - start) * 1000))


def _error_record(target: str, start: float, reason: str) -> Record:
    return Record(target, "error", 0, 0, "", _elapsed_ms(start), reason)


class Fetcher:
    """Holds one persistent HTTP connection and reuses it across requests
    (keep-alive). The stdlib has no high-level HTTP client with pooling, so we
    manage the connection by hand: lazily connect, reuse while healthy, and tear
    down + recreate on any error. This hand-rolled connection management is the
    concrete form of the 'stdlib HTTP is painful' finding for Python — the
    documented reason aiohttp is an allowed dependency, which we nonetheless
    avoid here to keep the dependency surface empty.

    The connection is created lazily (on first fetch), never in __init__, so that
    when the process pool forks workers, no socket file descriptor is shared
    across processes — each worker opens its own connection after the fork.
    """

    def __init__(self, base_url: str, timeout_s: float) -> None:
        u = urlparse(base_url)
        self._host = u.hostname
        self._https = u.scheme == "https"
        self._port = u.port or (443 if self._https else 80)
        self._timeout_s = timeout_s
        self._conn: http.client.HTTPConnection | None = None

    def _connect(self) -> None:
        if self._https:
            self._conn = http.client.HTTPSConnection(self._host, self._port, timeout=self._timeout_s)
        else:
            self._conn = http.client.HTTPConnection(self._host, self._port, timeout=self._timeout_s)

    def _reset(self) -> None:
        if self._conn is not None:
            try:
                self._conn.close()
            except Exception:
                pass
            self._conn = None

    def fetch(self, target: str) -> Record:
        """Run the full per-target pipeline. Never raises: every failure mode is
        folded into a status:"error" Record so a failed target cannot crash the
        run."""
        start = time.perf_counter()
        path = "/payload/" + target
        try:
            if self._conn is None:
                self._connect()
            assert self._conn is not None
            self._conn.request("GET", path)
            resp = self._conn.getresponse()

            status = resp.status
            if not (200 <= status < 300):
                resp.read()  # drain so the connection can be reused
                return _error_record(target, start, f"status {status}")

            proc = StreamProcessor()
            while True:
                chunk = resp.read(_CHUNK)
                if not chunk:
                    break
                proc.consume(chunk)

            return Record(
                target,
                "ok",
                proc.size(),
                proc.lines(),
                proc.sha_hex(),
                _elapsed_ms(start),
            )
        except Exception as e:
            # The connection may be in an unknown state; discard it.
            self._reset()
            return _error_record(target, start, f"fetch: {e}")


# --- worker-process entry points -------------------------------------------
# Each worker process holds its own Fetcher (and thus its own connection) in a
# module global, initialized once by the pool. process_one is what the pool maps
# over the targets; both are top-level and picklable.

_FETCHER: Fetcher | None = None


def init_worker(base_url: str, timeout_s: float) -> None:
    global _FETCHER
    _FETCHER = Fetcher(base_url, timeout_s)


def process_one(target: str) -> Record:
    assert _FETCHER is not None
    return _FETCHER.fetch(target)