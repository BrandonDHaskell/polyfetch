#ifndef POLYFETCH_REPORT_H
#define POLYFETCH_REPORT_H

#include <ostream>
#include <vector>

#include "fetch.hpp"

// Sorts records in place per the contract (§8): size_bytes DESC, then target
// ASC (lexicographic). Error records have size 0 and thus sort last.
void sort_records(std::vector<Record>& records);

// Writes the sorted records as JSON Lines followed by the summary footer,
// exactly per the output contract (§7): compact JSON, fixed key order, the
// `error` field present only on error records, one '\n' per line, single
// trailing newline.
void write_report(std::ostream& out, std::vector<Record>& records);

#endif // POLYFETCH_REPORT_H