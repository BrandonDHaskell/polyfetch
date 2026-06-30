package main

import (
	"bufio"
	"encoding/json"
	"io"
	"sort"
)

// Summary is the footer line emitted after all records. Field order is the JSON
// key order, pinned by the contract (§7.2). Do NOT reorder.
type Summary struct {
	Summary    bool  `json:"summary"`
	Total      int   `json:"total"`
	OK         int   `json:"ok"`
	Errors     int   `json:"errors"`
	TotalBytes int64 `json:"total_bytes"`
}

// sortRecords orders records per §8: size_bytes DESC, then target ASC
// (lexicographic byte order). Error records have size 0 so they naturally sort
// last, ordered among themselves by target ascending. This normalizes the
// nondeterministic completion order of the concurrent workers into a stable,
// identical-across-clients ordering.
func sortRecords(records []Record) {
	sort.Slice(records, func(i, j int) bool {
		if records[i].SizeBytes != records[j].SizeBytes {
			return records[i].SizeBytes > records[j].SizeBytes // DESC
		}
		return records[i].Target < records[j].Target // ASC
	})
}

// writeReport emits the sorted records as JSON Lines followed by the summary
// footer, exactly per the output contract (§7). encoding/json produces compact
// output (no spaces) by default and preserves struct field order, which is what
// the contract requires. Each record is terminated by a single '\n'.
func writeReport(w io.Writer, records []Record) error {
	sortRecords(records)

	bw := bufio.NewWriter(w)
	enc := json.NewEncoder(bw)
	// json.Encoder.Encode writes a trailing newline after each value, which is
	// exactly the JSON Lines framing we want. It does not HTML-escape unless
	// asked; disable escaping so e.g. '<' in a target id (shouldn't happen, but
	// be exact) is not mangled into \u003c.
	enc.SetEscapeHTML(false)

	var sum Summary
	sum.Summary = true
	sum.Total = len(records)
	for _, r := range records {
		if r.Status == "ok" {
			sum.OK++
			sum.TotalBytes += r.SizeBytes
		} else {
			sum.Errors++
		}
		if err := enc.Encode(&r); err != nil {
			return err
		}
	}

	if err := enc.Encode(&sum); err != nil {
		return err
	}

	return bw.Flush()
}
