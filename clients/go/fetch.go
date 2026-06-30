package main

import (
	"context"
	"fmt"
	"net/http"
	"time"
)

// Record is one line of output. The field order here is the JSON key order in
// the output (encoding/json preserves struct field declaration order), which
// the contract pins exactly. Do NOT reorder these fields.
//
// The `error` field uses omitempty so it is present only when status is
// "error" — exactly per the contract's §7.1.
type Record struct {
	Target    string `json:"target"`
	Status    string `json:"status"`
	SizeBytes int64  `json:"size_bytes"`
	LineCount int64  `json:"line_count"`
	SHA256    string `json:"sha256"`
	FetchMS   int64  `json:"fetch_ms"`
	Error     string `json:"error,omitempty"`
}

// fetchAndProcess performs the full per-target pipeline for one target: resolve
// the URL, GET it with a per-request timeout, and stream the body through the
// CPU stage. It never returns an error to its caller — every failure mode is
// folded into a Record with status "error", because the contract requires that
// a failed target not crash the run. The worker that calls this just forwards
// whatever Record comes back.
func fetchAndProcess(client *http.Client, baseURL, target string, timeout time.Duration) Record {
	start := time.Now()

	rec := Record{Target: target, Status: "ok"}

	url := fmt.Sprintf("%s/payload/%s", baseURL, target)

	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return errorRecord(target, start, fmt.Sprintf("request build: %v", err))
	}

	resp, err := client.Do(req)
	if err != nil {
		return errorRecord(target, start, fmt.Sprintf("fetch: %v", err))
	}
	defer resp.Body.Close()

	// Any non-2xx is a target-level error, recorded and skipped, not fatal.
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		// Drain a little so the connection can be reused, but we don't need
		// the body. processStream would also drain it, but on an error status
		// we treat the payload as absent per the contract (sha256 empty, sizes 0).
		return errorRecord(target, start, fmt.Sprintf("status %d", resp.StatusCode))
	}

	sha, size, lines, err := processStream(resp.Body)
	if err != nil {
		return errorRecord(target, start, fmt.Sprintf("read: %v", err))
	}

	rec.SHA256 = sha
	rec.SizeBytes = size
	rec.LineCount = lines
	rec.FetchMS = time.Since(start).Milliseconds()
	return rec
}

// errorRecord builds a status:"error" record with zeroed metrics, per §7.1.
func errorRecord(target string, start time.Time, reason string) Record {
	return Record{
		Target:    target,
		Status:    "error",
		SizeBytes: 0,
		LineCount: 0,
		SHA256:    "",
		FetchMS:   time.Since(start).Milliseconds(),
		Error:     reason,
	}
}
