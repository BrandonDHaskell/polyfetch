"""Aggregation, sorting, and byte-exact JSON Lines output."""

import json
from typing import TextIO

from fetch import Record


def sort_records(records: list[Record]) -> None:
    """Sort per the contract (§8): size_bytes DESC, then target ASC. The tuple
    key (-size_bytes, target) gives descending size (via negation) and ascending
    target. Error records have size 0 and thus sort last."""
    records.sort(key=lambda r: (-r.size_bytes, r.target))


def _record_to_json(r: Record) -> str:
    # Build the dict with keys in the contract's fixed order; Python dicts and
    # json.dumps both preserve insertion order. The `error` key is added only for
    # error records, as the last key.
    d: dict[str, object] = {
        "target": r.target,
        "status": r.status,
        "size_bytes": r.size_bytes,
        "line_count": r.line_count,
        "sha256": r.sha256,
        "fetch_ms": r.fetch_ms,
    }
    if r.status != "ok":
        d["error"] = r.error
    # separators=(",", ":") => compact (no spaces); ensure_ascii=False => raw
    # UTF-8 passthrough, matching the other clients' encoders.
    return json.dumps(d, separators=(",", ":"), ensure_ascii=False)


def write_report(records: list[Record], out: TextIO) -> None:
    """Write sorted records as JSON Lines followed by the summary footer, exactly
    per the output contract (§7): compact JSON, fixed key order, `error` only on
    error records, one '\\n' per line, single trailing newline."""
    sort_records(records)

    ok = 0
    errors = 0
    total_bytes = 0
    lines: list[str] = []
    for r in records:
        lines.append(_record_to_json(r))
        if r.status == "ok":
            ok += 1
            total_bytes += r.size_bytes
        else:
            errors += 1

    summary = json.dumps(
        {
            "summary": True,
            "total": len(records),
            "ok": ok,
            "errors": errors,
            "total_bytes": total_bytes,
        },
        separators=(",", ":"),
        ensure_ascii=False,
    )
    lines.append(summary)

    out.write("\n".join(lines) + "\n")