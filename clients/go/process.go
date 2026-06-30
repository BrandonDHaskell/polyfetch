package main

import (
	"crypto/sha256"
	"encoding/hex"
	"io"
)

// lineCounter is an io.Writer that counts newline (0x0A) bytes as they pass
// through. It writes nothing; it only tallies. This lets us count lines while
// streaming, without ever holding the payload in memory.
type lineCounter struct {
	count int64
}

func (lc *lineCounter) Write(p []byte) (int, error) {
	for _, b := range p {
		if b == '\n' {
			lc.count++
		}
	}
	return len(p), nil
}

// processStream consumes r to completion, simultaneously computing the SHA-256,
// the total byte count, and the newline count. The payload is never buffered:
// bytes flow from r through an io.MultiWriter into the hasher and the line
// counter, and io.Copy's return value gives us the size for free.
//
// This is the CPU-bound stage of the pipeline. Note that it shares the same
// goroutine as the fetch — Go needs no structural separation between the I/O
// stage and the CPU stage, which is one of the central observations of this
// experiment.
func processStream(r io.Reader) (sha string, sizeBytes int64, lineCount int64, err error) {
	hasher := sha256.New()
	lc := &lineCounter{}

	// Tee every byte into both the hasher and the line counter as it arrives.
	sink := io.MultiWriter(hasher, lc)

	n, err := io.Copy(sink, r)
	if err != nil {
		return "", 0, 0, err
	}

	return hex.EncodeToString(hasher.Sum(nil)), n, lc.count, nil
}
