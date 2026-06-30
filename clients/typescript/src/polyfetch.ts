import { Worker } from "worker_threads";
import * as path from "path";
import * as fs from "fs";
import type { TargetRecord } from "./fetch";
import { writeReport } from "./report";

// Exit codes, per the contract (§9):
//   0 — run completed (per-target errors do NOT change this)
//   1 — the run itself failed to complete
//   2 — invalid invocation (bad flag, unreadable targets file)
const EXIT_RUN_FAIL = 1;
const EXIT_USAGE = 2;

interface Config {
  concurrency: number;
  targetsPath: string;
  baseUrl: string;
  timeoutMs: number;
}

function usageError(msg: string): never {
  process.stderr.write(`polyfetch: ${msg}\n`);
  process.exit(EXIT_USAGE);
}

function parseArgs(argv: string[]): Config {
  const cfg: Config = {
    concurrency: 8,
    targetsPath: "shared/targets.txt",
    baseUrl: "http://localhost:8080",
    timeoutMs: 30000,
  };

  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    const needValue = (name: string): string => {
      if (i + 1 >= argv.length) usageError(`missing value for ${name}`);
      return argv[++i];
    };
    switch (arg) {
      case "--concurrency": {
        const v = needValue("--concurrency");
        const n = Number(v);
        if (!Number.isInteger(n) || n < 1) usageError("--concurrency must be an integer >= 1");
        cfg.concurrency = n;
        break;
      }
      case "--targets":
        cfg.targetsPath = needValue("--targets");
        break;
      case "--base-url":
        cfg.baseUrl = needValue("--base-url");
        break;
      case "--timeout": {
        const v = needValue("--timeout");
        const n = Number(v);
        if (!Number.isInteger(n) || n < 0) usageError("--timeout must be an integer >= 0");
        cfg.timeoutMs = n;
        break;
      }
      case "-h":
      case "--help":
        process.stdout.write(
          "usage: polyfetch [--concurrency K] [--targets PATH] [--base-url URL] [--timeout MS]\n",
        );
        process.exit(0);
      default:
        usageError(`unknown flag: ${arg}`);
    }
  }

  return cfg;
}

// Read targets: one per line, skipping blank lines and lines beginning with
// '#'. The remainder of each line (including any "?size=...&delay=..." query) is
// preserved verbatim. Returns null if the file cannot be read.
function readTargets(p: string): string[] | null {
  let text: string;
  try {
    text = fs.readFileSync(p, "utf-8");
  } catch {
    return null;
  }
  const out: string[] = [];
  for (const raw of text.split("\n")) {
    const line = raw.trim();
    if (line === "" || line.startsWith("#")) continue;
    out.push(line);
  }
  return out;
}

/**
 * The concurrency core. Spawns a bounded pool of K worker threads and feeds each
 * one target at a time, so at most K targets are in flight — the contract's
 * concurrency bound. Each worker runs the full fetch + process pipeline on its
 * own thread (true CPU parallelism for the hashing stage).
 *
 * Contrast with the C++ client: there, the shared work queue needed a
 * std::mutex because multiple OS threads touched it. Here, the coordination
 * state (nextIndex, completed) lives on the single-threaded main event loop, so
 * message handlers run sequentially and NO lock is needed. The single-
 * threadedness that forces us into worker_threads for CPU work is, for the
 * coordination itself, a simplifying asset.
 */
function run(config: Config, targets: string[]): Promise<TargetRecord[]> {
  return new Promise((resolve, reject) => {
    const n = targets.length;
    const results: TargetRecord[] = new Array(n);
    if (n === 0) {
      resolve(results);
      return;
    }

    const k = Math.min(config.concurrency, n);
    let nextIndex = 0;
    let completed = 0;

    const workerPath = path.join(__dirname, "worker.js");
    const workers: Worker[] = [];

    const assignNext = (worker: Worker): void => {
      if (nextIndex < n) {
        const idx = nextIndex++;
        worker.postMessage({ index: idx, target: targets[idx] });
      }
      // else: no work left; the worker idles until terminated below.
    };

    for (let i = 0; i < k; i++) {
      const worker = new Worker(workerPath, {
        workerData: { baseUrl: config.baseUrl, timeoutMs: config.timeoutMs },
      });
      workers.push(worker);

      worker.on("message", (msg: { index: number; record: TargetRecord }) => {
        results[msg.index] = msg.record;
        completed++;
        if (completed === n) {
          // All targets processed; every worker is idle. Tear them down, then
          // resolve. We do not call process.exit here — letting the event loop
          // drain ensures the stdout write in writeReport flushes fully.
          Promise.all(workers.map((w) => w.terminate())).then(() => resolve(results));
        } else {
          assignNext(worker);
        }
      });

      worker.on("error", (err) => {
        reject(err);
      });
    }

    // Kick off: give each worker its first task.
    for (const w of workers) assignNext(w);
  });
}

async function main(): Promise<void> {
  const cfg = parseArgs(process.argv.slice(2));

  const targets = readTargets(cfg.targetsPath);
  if (targets === null) {
    process.stderr.write(`polyfetch: cannot read targets file: ${cfg.targetsPath}\n`);
    process.exit(EXIT_USAGE);
  }

  let records: TargetRecord[];
  try {
    records = await run(cfg, targets);
  } catch (err) {
    const e = err as { message?: string };
    process.stderr.write(`polyfetch: run failed: ${e.message ?? "error"}\n`);
    process.exit(EXIT_RUN_FAIL);
  }

  writeReport(records);
  // Natural exit (code 0) after stdout drains and workers are terminated.
}

void main();
