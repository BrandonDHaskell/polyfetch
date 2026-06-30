package main

import (
	"bufio"
	"fmt"
	"net/http"
	"os"
	"strings"
	"sync"
	"time"
)

// Exit codes, per the contract (§9):
//
//	0 — run completed (per-target errors do NOT change this)
//	1 — the run itself failed to complete
//	2 — invalid invocation (bad flag, unreadable targets file)
const (
	exitOK      = 0
	exitRunFail = 1
	exitUsage   = 2
)

type config struct {
	concurrency int
	targetsPath string
	baseURL     string
	timeout     time.Duration
}

func main() {
	cfg, code, done := parseArgs(os.Args[1:])
	if done {
		os.Exit(code)
	}

	targets, err := readTargets(cfg.targetsPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "polyfetch: %v\n", err)
		os.Exit(exitUsage)
	}

	records := run(cfg, targets)

	if err := writeReport(os.Stdout, records); err != nil {
		fmt.Fprintf(os.Stderr, "polyfetch: writing report: %v\n", err)
		os.Exit(exitRunFail)
	}

	os.Exit(exitOK)
}

// run is the concurrency core. K worker goroutines pull targets off a jobs
// channel, each running the full fetch+process pipeline, and push Records onto
// a results channel. A bounded number of workers (K = cfg.concurrency) is what
// enforces "at most K targets in flight" — the jobs channel provides natural
// backpressure, and no worker starts a new target until it finishes its current
// one. This single mechanism covers BOTH the I/O stage and the CPU stage; Go
// requires no structural change between them.
func run(cfg config, targets []string) []Record {
	client := newHTTPClient(cfg)

	jobs := make(chan string)
	results := make(chan Record)

	var wg sync.WaitGroup
	for i := 0; i < cfg.concurrency; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for target := range jobs {
				results <- fetchAndProcess(client, cfg.baseURL, target, cfg.timeout)
			}
		}()
	}

	// Feed jobs in a separate goroutine so we can start draining results
	// immediately and never deadlock on an unbuffered channel.
	go func() {
		for _, t := range targets {
			jobs <- t
		}
		close(jobs)
	}()

	// Close results once all workers are done, so the range below terminates.
	go func() {
		wg.Wait()
		close(results)
	}()

	records := make([]Record, 0, len(targets))
	for rec := range results {
		records = append(records, rec)
	}
	return records
}

// newHTTPClient builds the shared HTTP client. The two settings that matter for
// measurement honesty:
//   - MaxIdleConnsPerHost: Go defaults to 2, which would throttle high
//     concurrency by tearing down and rebuilding connections. We size it to the
//     concurrency level so the connection pool is never the bottleneck — we want
//     to measure the language's concurrency, not TCP handshake overhead.
//   - Per-request timeouts are applied via context in fetchAndProcess, not here,
//     so --timeout bounds each target individually.
func newHTTPClient(cfg config) *http.Client {
	tr := http.DefaultTransport.(*http.Transport).Clone()
	tr.MaxIdleConns = cfg.concurrency * 2
	tr.MaxIdleConnsPerHost = cfg.concurrency * 2
	return &http.Client{Transport: tr}
}

// readTargets reads the targets file: one target per line, skipping blank lines
// and lines beginning with '#'. The remainder of each line (e.g. an inline
// "?size=...&delay=...") is preserved as part of the target, since the contract
// resolves <base-url>/payload/<target> verbatim.
func readTargets(path string) ([]string, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("opening targets file %q: %w", path, err)
	}
	defer f.Close()

	var targets []string
	sc := bufio.NewScanner(f)
	// Allow long lines (large query strings) beyond the default 64KiB token.
	sc.Buffer(make([]byte, 0, 64*1024), 1024*1024)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		targets = append(targets, line)
	}
	if err := sc.Err(); err != nil {
		return nil, fmt.Errorf("reading targets file %q: %w", path, err)
	}
	return targets, nil
}
