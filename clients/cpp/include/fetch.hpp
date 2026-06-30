#ifndef POLYFETCH_FETCH_H
#define POLYFETCH_FETCH_H

#include <cstdint>
#include <string>

#include <curl/curl.h>

// One output record. The field order here is the JSON key order the contract
// pins exactly (§7.1): target, status, size_bytes, line_count, sha256,
// fetch_ms, error. The `error` field is emitted only when status == "error".
struct Record {
    std::string target;
    std::string status;      // "ok" or "error"
    std::int64_t size_bytes = 0;
    std::int64_t line_count = 0;
    std::string sha256;      // 64 lowercase hex chars, or "" on error
    std::int64_t fetch_ms = 0;
    std::string error;       // present only when status == "error"
};

// Performs the full per-target pipeline for one target using the provided curl
// handle (reused across calls within a worker thread for connection pooling).
// Never throws to the caller and never reports failure out of band: every
// failure mode is folded into a Record with status "error", because the
// contract requires a failed target not to crash the run.
//
//   handle    — a curl easy handle owned by the calling thread
//   base_url  — e.g. "http://localhost:8080"
//   target    — the verbatim line from targets.txt (may include a query string)
//   timeout_ms— per-target timeout
Record fetch_and_process(CURL* handle,
                         const std::string& base_url,
                         const std::string& target,
                         long timeout_ms);

#endif // POLYFETCH_FETCH_H