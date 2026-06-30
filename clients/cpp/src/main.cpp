#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <curl/curl.h>

#include "fetch.hpp"
#include "report.hpp"

// Exit codes, per the contract (§9):
//   0 — run completed (per-target errors do NOT change this)
//   1 — the run itself failed to complete
//   2 — invalid invocation (bad flag, unreadable targets file)
namespace {
constexpr int kExitOK = 0;
constexpr int kExitRunFail = 1;
constexpr int kExitUsage = 2;

struct Config {
    int concurrency = 8;
    std::string targets_path = "shared/targets.txt";
    std::string base_url = "http://localhost:8080";
    long timeout_ms = 30000;
};

void usage_error(const std::string& msg) {
    std::cerr << "polyfetch: " << msg << "\n";
    std::exit(kExitUsage);
}

// Parse "--flag value" pairs. Unknown flags or missing/invalid values exit 2.
Config parse_args(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto need_value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) usage_error(std::string("missing value for ") + name);
            return argv[++i];
        };
        if (arg == "--concurrency") {
            std::string v = need_value("--concurrency");
            char* end = nullptr;
            long n = std::strtol(v.c_str(), &end, 10);
            if (*end != '\0' || n < 1 || n > std::numeric_limits<int>::max()) usage_error("--concurrency must be an integer >= 1");
            cfg.concurrency = static_cast<int>(n);
        } else if (arg == "--targets") {
            cfg.targets_path = need_value("--targets");
        } else if (arg == "--base-url") {
            cfg.base_url = need_value("--base-url");
        } else if (arg == "--timeout") {
            std::string v = need_value("--timeout");
            char* end = nullptr;
            long n = std::strtol(v.c_str(), &end, 10);
            if (*end != '\0' || n < 0) usage_error("--timeout must be an integer >= 0");
            cfg.timeout_ms = n;
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "usage: polyfetch [--concurrency K] [--targets PATH] "
                         "[--base-url URL] [--timeout MS]\n";
            std::exit(kExitOK);
        } else {
            usage_error("unknown flag: " + arg);
        }
    }
    return cfg;
}

// Read targets: one per line, skipping blank lines and lines beginning with
// '#'. The remainder of each line (including any "?size=...&delay=..." query)
// is preserved verbatim, since the contract resolves <base-url>/payload/<line>.
// Returns false if the file cannot be opened.
bool read_targets(const std::string& path, std::vector<std::string>& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        // Trim leading/trailing whitespace (incl. a trailing '\r' from CRLF).
        size_t b = line.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) continue;
        size_t e = line.find_last_not_of(" \t\r\n");
        std::string t = line.substr(b, e - b + 1);
        if (t.empty() || t[0] == '#') continue;
        out.push_back(std::move(t));
    }
    return true;
}

// The concurrency core. K worker threads drain a shared work queue of target
// indices. The bound on K is what enforces "at most K targets in flight": each
// thread processes exactly one target at a time, and there are K threads.
//
// Contrast with the Go client: where Go writes `jobs := make(chan string)` and
// ranges over it, here we hand-roll the equivalent — a mutex-guarded index into
// the shared targets vector. The channel Go gives us for free is, in C++,
// explicit machinery we assemble ourselves. Notably we need NO condition
// variable: the workload is fully known up front, so the queue only shrinks and
// workers never wait for new work — they take the next index or find none left
// and exit. The mutex alone suffices.
//
// As in Go, each worker runs BOTH stages (fetch then process) in one pass on
// the same thread; C++ needs no structural separation between I/O and CPU work.
//
// Results are written to per-index slots (results[i] for index i), and since
// each index is handled by exactly one thread, those writes need no lock — only
// the shared next-index counter does.
std::vector<Record> run(const Config& cfg, const std::vector<std::string>& targets) {
    const size_t n = targets.size();
    std::vector<Record> results(n);

    // atomic fetch_add gives each worker a unique index with no lock overhead.
    std::atomic<size_t> next_index{0};

    auto worker = [&]() {
        // Each thread owns its own curl handle. libcurl easy handles must not be
        // shared between threads; one-per-thread is the standard pattern, and
        // reusing the handle across requests pools connections (the analog of
        // Go's MaxIdleConnsPerHost tuning).
        CURL* handle = curl_easy_init();

        for (;;) {
            size_t i = next_index.fetch_add(1, std::memory_order_relaxed);
            if (i >= n) break;
            if (!handle) {
                results[i].target = targets[i];
                results[i].status = "error";
                results[i].error = "curl_easy_init failed";
            } else {
                results[i] = fetch_and_process(handle, cfg.base_url, targets[i], cfg.timeout_ms);
            }
        }

        if (handle) curl_easy_cleanup(handle);
    };

    int k = cfg.concurrency;
    if (static_cast<size_t>(k) > n && n > 0) k = static_cast<int>(n);  // no idle threads

    std::vector<std::thread> pool;
    pool.reserve(k);
    for (int t = 0; t < k; ++t) pool.emplace_back(worker);
    for (auto& th : pool) th.join();

    return results;
}

}  // namespace

int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

    Config cfg = parse_args(argc, argv);

    std::vector<std::string> targets;
    if (!read_targets(cfg.targets_path, targets)) {
        std::cerr << "polyfetch: cannot read targets file: " << cfg.targets_path << "\n";
        return kExitUsage;
    }

    // curl_global_init is NOT thread-safe and must run before any worker
    // threads are spawned.
    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
        std::cerr << "polyfetch: curl_global_init failed\n";
        return kExitRunFail;
    }

    std::vector<Record> records = run(cfg, targets);

    write_report(std::cout, records);

    curl_global_cleanup();
    return kExitOK;
}