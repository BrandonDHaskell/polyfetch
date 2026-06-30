#ifndef POLYFETCH_PROCESS_H
#define POLYFETCH_PROCESS_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "sha256.hpp"

// Accumulates byte count, '\n' line count, and SHA-256 digest as data streams
// in. Fed by the libcurl write callback one chunk at a time; never buffers the
// full payload in memory.
class StreamProcessor {
public:
    void consume(const unsigned char* data, std::size_t len);

    std::int64_t size_bytes() const { return size_bytes_; }
    std::int64_t line_count() const { return line_count_; }

    // Finalizes and returns the lowercase hex digest. Mutates internal SHA-256
    // state; call once per target after all consume() calls.
    std::string sha_hex() { return sha_.hex_digest(); }

private:
    Sha256 sha_;
    std::int64_t size_bytes_ = 0;
    std::int64_t line_count_ = 0;
};

#endif  // POLYFETCH_PROCESS_H
