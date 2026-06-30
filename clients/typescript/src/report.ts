import type { TargetRecord } from "./fetch";

/**
 * Sort records per the contract (§8): size_bytes DESC, then target ASC
 * (lexicographic). For ASCII target ids, JS string comparison matches byte
 * order. Error records have size 0 and thus sort last.
 */
export function sortRecords(records: TargetRecord[]): void {
  records.sort((a, b) => {
    if (a.size_bytes !== b.size_bytes) return b.size_bytes - a.size_bytes; // DESC
    return a.target < b.target ? -1 : a.target > b.target ? 1 : 0; // ASC
  });
}

/**
 * Serialize one record as compact JSON with the contract's fixed key order. We
 * build a fresh object with keys inserted in order (rather than trusting the
 * incoming object's key order, which crossed a worker boundary), then let
 * JSON.stringify produce compact output and correct string escaping. The
 * `error` key is included only for error records, as the last key.
 */
function recordToJson(r: TargetRecord): string {
  const ordered: Record<string, unknown> = {
    target: r.target,
    status: r.status,
    size_bytes: r.size_bytes,
    line_count: r.line_count,
    sha256: r.sha256,
    fetch_ms: r.fetch_ms,
  };
  if (r.status !== "ok") {
    ordered.error = r.error ?? "";
  }
  return JSON.stringify(ordered);
}

/**
 * Write the sorted records as JSON Lines followed by the summary footer,
 * exactly per the output contract (§7): compact JSON, fixed key order, the
 * `error` field present only on error records, one "\n" per line, single
 * trailing newline. The whole report is written in one stdout call.
 */
export function writeReport(records: TargetRecord[]): void {
  sortRecords(records);

  let ok = 0;
  let errors = 0;
  let totalBytes = 0;

  const lines: string[] = [];
  for (const r of records) {
    lines.push(recordToJson(r));
    if (r.status === "ok") {
      ok++;
      totalBytes += r.size_bytes;
    } else {
      errors++;
    }
  }

  const summary = JSON.stringify({
    summary: true,
    total: records.length,
    ok,
    errors,
    total_bytes: totalBytes,
  });
  lines.push(summary);

  // Trailing "\n" after the summary gives a single final newline.
  process.stdout.write(lines.join("\n") + "\n");
}
