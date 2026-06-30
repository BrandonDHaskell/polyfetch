import { parentPort, workerData } from "worker_threads";
import type { WorkerConfig } from "./types";
import { fetchAndProcess, type TargetRecord } from "./fetch";

/**
 * Worker thread entry point. Each worker is a self-contained fetch + process
 * pipeline — the direct analog of a Go goroutine or a C++ worker thread. It runs
 * the I/O stage (async fetch) and the CPU stage (streaming SHA-256) on its own
 * thread, with its own V8 isolate, so multiple workers hash in parallel across
 * cores. This is the mechanism that has NO analog in the Go and C++ clients,
 * where one goroutine/thread handled both stages without a separate worker pool.
 *
 * Protocol: the main thread posts { index, target } tasks. The worker processes
 * exactly one at a time and replies { index, record }. The main thread feeds it
 * the next task on reply, so at most one target is in flight per worker (and
 * thus at most K across K workers — the contract's concurrency bound).
 */

interface Task {
  index: number;
  target: string;
}

const config = workerData as WorkerConfig;

if (!parentPort) {
  // Not running as a worker; nothing to do.
  process.exit(1);
}

const port = parentPort;

port.on("message", async (task: Task) => {
  const record: TargetRecord = await fetchAndProcess(config.baseUrl, task.target, config.timeoutMs);
  port.postMessage({ index: task.index, record });
});
