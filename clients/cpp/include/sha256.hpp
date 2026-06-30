#ifndef POLYFETCH_SHA256_H
#define POLYFETCH_SHA256_H

#include <cstdint>
#include <cstddef>
#include <string>

// Streaming SHA-256 (FIPS 180-4).
//
// Vendored deliberately. The C++ standard library provides no cryptographic
// hashing, and rather than add a second external dependency (OpenSSL) beyond
// libcurl, we implement the algorithm here. SHA-256 is a fixed published
// standard and a leaf utility — not concurrency machinery — so vendoring it
// keeps the experiment's dependency surface to exactly libcurl while leaving
// the thing under study (the concurrency model) untouched.
//
// The interface is incremental (update/finalize) so the HTTP layer can feed it
// chunks as they stream in, without ever buffering the whole payload — mirroring
// the Go client's io.Copy-into-hasher design.
class Sha256 {
public:
    Sha256() { reset(); }

    void reset();

    // Feed `len` bytes from `data` into the hash.
    void update(const unsigned char* data, std::size_t len);

    // Produce the lowercase hex digest (64 chars). Consumes the internal state;
    // call reset() before reusing the object for another message.
    std::string hex_digest();

private:
    void process_block(const unsigned char* block);

    std::uint32_t state_[8];
    std::uint64_t bit_len_;
    unsigned char buffer_[64];
    std::size_t buffer_len_;
};

#endif // POLYFETCH_SHA256_H