import { StreamProcessor } from "./process";

/**
 * One output record. The property order here is the JSON key order the contract
 * pins exactly (§7.1): target, status, size_bytes, line_count, sha256,
 * fetch_ms, error. The `error` field is present only when status === "error"
 * (report.ts enforces the ordering and omission explicitly).
 */
export interface TargetRecord {
  target: string;
  status: "ok" | "error";
  size_bytes: number;
  line_count: number;
  sha256: string;
  fetch_ms: number;
  error?: string;
}

function errorRecord(target: string, fetchMs: number, reason: string): TargetRecord {
  return {
    target,
    status: "error",
    size_bytes: 0,
    line_count: 0,
    sha256: "",
    fetch_ms: fetchMs,
    error: reason,
  };
}

/**
 * The full per-target pipeline for one target: resolve the URL, GET it with a
 * per-target timeout, and stream the body through the CPU stage. Never throws
 * to the caller and never reports failure out of band — every failure mode is
 * folded into a TargetRecord with status "error", because the contract requires
 * that a failed target not crash the run.
 *
 * The body is consumed via a streaming reader (getReader), so payload bytes flow
 * straight into the hasher and counters without ever being buffered whole —
 * matching the Go and C++ clients. This runs inside a worker thread.
 */
export async function fetchAndProcess(
  baseUrl: string,
  target: string,
  timeoutMs: number,
): Promise<TargetRecord> {
  const start = performance.now();
  const elapsed = () => Math.round(performance.now() - start);

  const url = `${baseUrl}/payload/${target}`;

  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);

  try {
    const resp = await fetch(url, { signal: controller.signal });

    // Any non-2xx is a target-level error, recorded and skipped, not fatal. We
    // do not read the body in that case (payload treated as absent), matching
    // the Go and C++ clients.
    if (!resp.ok) {
      return errorRecord(target, elapsed(), `status ${resp.status}`);
    }

    const proc = new StreamProcessor();
    if (resp.body) {
      const reader = resp.body.getReader();
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        if (value) proc.consume(value);
      }
    }

    return {
      target,
      status: "ok",
      size_bytes: proc.size(),
      line_count: proc.lines(),
      sha256: proc.shaHex(),
      fetch_ms: elapsed(),
    };
  } catch (err) {
    const e = err as { name?: string; message?: string };
    const reason =
      e.name === "AbortError" ? "fetch: timeout" : `fetch: ${e.message ?? "error"}`;
    return errorRecord(target, elapsed(), reason);
  } finally {
    clearTimeout(timer);
  }
}
