package main

import (
	"flag"
	"fmt"
	"os"
	"time"
)

// parseArgs parses the CLI per the contract (§5). It returns the config, an
// exit code, and a "done" flag indicating main should exit immediately with
// that code (used for -h and for validation failures, which exit 2).
//
// Flags (all with the exact defaults the contract pins):
//
//	--concurrency K   int >= 1   default 8    max targets in flight
//	--targets PATH    path       default shared/targets.txt
//	--base-url URL    string     default http://localhost:8080
//	--timeout MS      int ms     default 30000
//
// Invalid values (e.g. --concurrency 0) exit with code 2 and a message to
// stderr, per the contract.
func parseArgs(args []string) (cfg config, code int, done bool) {
	fs := flag.NewFlagSet("polyfetch", flag.ContinueOnError)

	concurrency := fs.Int("concurrency", 8, "max targets in flight simultaneously (>=1)")
	targets := fs.String("targets", "shared/targets.txt", "path to targets file")
	baseURL := fs.String("base-url", "http://localhost:8080", "server base URL")
	timeoutMS := fs.Int("timeout", 30000, "per-target timeout in milliseconds")

	if err := fs.Parse(args); err != nil {
		// flag already printed usage/error to stderr.
		return config{}, exitUsage, true
	}

	if *concurrency < 1 {
		fmt.Fprintf(os.Stderr, "polyfetch: --concurrency must be >= 1 (got %d)\n", *concurrency)
		return config{}, exitUsage, true
	}
	if *timeoutMS < 0 {
		fmt.Fprintf(os.Stderr, "polyfetch: --timeout must be >= 0 (got %d)\n", *timeoutMS)
		return config{}, exitUsage, true
	}

	cfg = config{
		concurrency: *concurrency,
		targetsPath: *targets,
		baseURL:     *baseURL,
		timeout:     time.Duration(*timeoutMS) * time.Millisecond,
	}
	return cfg, exitOK, false
}
