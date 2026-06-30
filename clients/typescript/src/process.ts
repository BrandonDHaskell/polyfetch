import { createHash, type Hash } from "crypto";

/**
 * The CPU-bound stage. Bytes are fed in as they arrive from the network
 * (consume), and it simultaneously updates the SHA-256, the total byte count,
 * and the newline (0x0A) count. The payload is never buffered in full — this
 * mirrors the Go client's io.Copy into an io.MultiWriter and the C++ client's
 * StreamProcessor.
 *
 * Crucially, this runs INSIDE a worker thread (see worker.ts). On Node's single
 * main-thread event loop, SHA-256 hashing is synchronous native work that
 * blocks the loop, so hashes cannot run in parallel there. Moving the whole
 * pipeline into worker_threads is what buys real CPU parallelism — the
 * structural change that Go and C++ did not need, and the central observation
 * this client is built to demonstrate.
 */
export class StreamProcessor {
  private hash: Hash = createHash("sha256");
  private sizeBytes = 0;
  private lineCount = 0;

  consume(chunk: Uint8Array): void {
    this.hash.update(chunk);
    this.sizeBytes += chunk.length;
    for (let i = 0; i < chunk.length; i++) {
      if (chunk[i] === 0x0a) this.lineCount++;
    }
  }

  // Single-use: digest() finalizes the hash; calling consume() afterward throws.
  // Node's digest("hex") returns lowercase hex, matching the contract.
  shaHex(): string {
    return this.hash.digest("hex");
  }

  size(): number {
    return this.sizeBytes;
  }

  lines(): number {
    return this.lineCount;
  }
}
