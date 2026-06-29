package main

import (
	"context"
	"encoding/binary"
	"flag"
	"fmt"
	"hash/fnv"
	"log"
	"math/rand/v2"
	"net/http"
	"os"
	"os/signal"
	"strconv"
	"syscall"
	"time"
)

const (
	defaultSize = 1 << 20 // 1 MiB
	chunkSize   = 32 * 1024
)

// seedFromID derives two independent uint64 seeds from a target ID string using
// two separate FNV-64a hashes with different domain separators, giving PCG the
// independent seeds it needs to avoid degenerate state-space regions.
func seedFromID(id string) (uint64, uint64) {
	h1 := fnv.New64a()
	h1.Write([]byte(id))
	s1 := h1.Sum64()

	h2 := fnv.New64a()
	h2.Write([]byte("polyfetch:")) // domain separator
	h2.Write([]byte(id))
	s2 := h2.Sum64()
	return s1, s2
}

// fillBuf fills buf with deterministic pseudo-random bytes from rng.
func fillBuf(rng *rand.Rand, buf []byte) {
	i := 0
	for i+8 <= len(buf) {
		binary.LittleEndian.PutUint64(buf[i:], rng.Uint64())
		i += 8
	}
	if i < len(buf) {
		var tail [8]byte
		binary.LittleEndian.PutUint64(tail[:], rng.Uint64())
		copy(buf[i:], tail[:len(buf)-i])
	}
}

func handlePayload(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	q := r.URL.Query()

	size := defaultSize
	if s := q.Get("size"); s != "" {
		n, err := strconv.Atoi(s)
		if err != nil || n < 0 {
			http.Error(w, "invalid size", http.StatusBadRequest)
			return
		}
		size = n
	}

	delay := 0
	if d := q.Get("delay"); d != "" {
		n, err := strconv.Atoi(d)
		if err != nil || n < 0 {
			http.Error(w, "invalid delay", http.StatusBadRequest)
			return
		}
		delay = n
	}

	status := http.StatusOK
	if s := q.Get("status"); s != "" {
		n, err := strconv.Atoi(s)
		if err != nil || n < 100 || n > 599 {
			http.Error(w, "invalid status", http.StatusBadRequest)
			return
		}
		status = n
	}

	if delay > 0 {
		time.Sleep(time.Duration(delay) * time.Millisecond)
	}

	// No Content-Length: use chunked transfer encoding so a client disconnect
	// mid-stream never leaves the connection in a broken state.
	w.Header().Set("Content-Type", "application/octet-stream")
	w.WriteHeader(status)

	if status < 200 || status >= 300 {
		// Simulated upstream error: empty body by design. Validation errors above
		// get human-readable text bodies; ?status= errors get nothing, matching
		// real upstream behavior clients must handle correctly.
		return
	}

	s1, s2 := seedFromID(id)
	rng := rand.New(rand.NewPCG(s1, s2))

	buf := make([]byte, chunkSize)
	for remaining := size; remaining > 0; {
		n := remaining
		if n > chunkSize {
			n = chunkSize
		}
		fillBuf(rng, buf[:n])
		if _, err := w.Write(buf[:n]); err != nil {
			return
		}
		remaining -= n
	}
}

func logMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		next.ServeHTTP(w, r)
		log.Printf("%s %s %s", r.Method, r.URL.RequestURI(), time.Since(start))
	})
}

func main() {
	addr := flag.String("addr", ":8080", "listen address")
	quiet := flag.Bool("quiet", false, "suppress per-request logs")
	healthcheck := flag.Bool("healthcheck", false, "check server health and exit")
	flag.Parse()

	if *healthcheck {
		resp, err := http.Get("http://localhost" + *addr + "/health")
		if err != nil {
			log.Printf("healthcheck failed: %v", err)
			os.Exit(1)
		}
		resp.Body.Close()
		if resp.StatusCode != http.StatusOK {
			log.Printf("healthcheck: unexpected status %d", resp.StatusCode)
			os.Exit(1)
		}
		os.Exit(0)
	}

	mux := http.NewServeMux()
	mux.HandleFunc("GET /payload/{id}", handlePayload)
	mux.HandleFunc("GET /health", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintln(w, "ok")
	})

	var handler http.Handler = mux
	if !*quiet {
		handler = logMiddleware(mux)
	}

	srv := &http.Server{
		Addr:    *addr,
		Handler: handler,
		// Bounds how long a client can take to send request headers; safe to
		// keep short because headers always arrive in milliseconds.
		// ReadTimeout and WriteTimeout are deliberately unset: ?delay= and large
		// ?size= requests are legitimate slow responses that would trip them.
		ReadHeaderTimeout: 5 * time.Second,
		IdleTimeout:       60 * time.Second,
	}

	go func() {
		log.Printf("polyfetch server listening on %s", *addr)
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatal(err)
		}
	}()

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	<-sigCh

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	if err := srv.Shutdown(ctx); err != nil {
		log.Printf("shutdown error: %v", err)
	}
}
