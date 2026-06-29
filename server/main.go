package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"hash/fnv"
	"log"
	"math/rand/v2"
	"net/http"
	"strconv"
	"time"
)

const (
	defaultSize = 1 << 20 // 1 MiB
	chunkSize   = 32 * 1024
)

// seedFromID derives two independent uint64 seeds from a target ID string.
// Using FNV-64a and its bitwise complement gives two uncorrelated seeds for
// the PCG source, ensuring different IDs produce different byte streams.
func seedFromID(id string) (uint64, uint64) {
	h := fnv.New64a()
	h.Write([]byte(id))
	s := h.Sum64()
	return s, ^s
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

	w.Header().Set("Content-Type", "application/octet-stream")
	if status >= 200 && status < 300 {
		w.Header().Set("Content-Length", strconv.Itoa(size))
	}
	w.WriteHeader(status)

	if status < 200 || status >= 300 {
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

func main() {
	addr := flag.String("addr", ":8080", "listen address")
	flag.Parse()

	mux := http.NewServeMux()
	mux.HandleFunc("GET /payload/{id}", handlePayload)
	mux.HandleFunc("GET /health", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintln(w, "ok")
	})

	log.Printf("polyfetch server listening on %s", *addr)
	log.Fatal(http.ListenAndServe(*addr, mux))
}
