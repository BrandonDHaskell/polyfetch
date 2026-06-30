#include "report.hpp"

#include <algorithm>
#include <string>

namespace {

// Escape a string for embedding in a JSON string literal. Handles the
// characters JSON requires: quote, backslash, and control chars < 0x20. Target
// ids and hex digests never need this, but error reasons (which include curl
// messages) might, so we apply it to all string fields for correctness.
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 0x0f];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

// Emit one record as a compact JSON object with the contract's fixed key order.
void write_record(std::ostream& out, const Record& r) {
    out << "{\"target\":\"" << json_escape(r.target) << "\""
        << ",\"status\":\"" << r.status << "\""
        << ",\"size_bytes\":" << r.size_bytes
        << ",\"line_count\":" << r.line_count
        << ",\"sha256\":\"" << r.sha256 << "\""
        << ",\"fetch_ms\":" << r.fetch_ms;
    if (r.status != "ok") {
        out << ",\"error\":\"" << json_escape(r.error) << "\"";
    }
    out << "}\n";
}

}  // namespace

void sort_records(std::vector<Record>& records) {
    std::sort(records.begin(), records.end(),
              [](const Record& a, const Record& b) {
                  if (a.size_bytes != b.size_bytes) {
                      return a.size_bytes > b.size_bytes;  // DESC
                  }
                  return a.target < b.target;  // ASC
              });
}

void write_report(std::ostream& out, std::vector<Record>& records) {
    sort_records(records);

    int ok = 0, errors = 0;
    std::int64_t total_bytes = 0;
    for (const auto& r : records) {
        write_record(out, r);
        if (r.status == "ok") {
            ++ok;
            total_bytes += r.size_bytes;
        } else {
            ++errors;
        }
    }

    out << "{\"summary\":true"
        << ",\"total\":" << records.size()
        << ",\"ok\":" << ok
        << ",\"errors\":" << errors
        << ",\"total_bytes\":" << total_bytes
        << "}\n";
}