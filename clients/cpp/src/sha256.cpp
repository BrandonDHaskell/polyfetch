#include "sha256.hpp"

#include <cstring>

namespace {

// First 32 bits of the fractional parts of the cube roots of the first 64
// primes (FIPS 180-4, §4.2.2).
constexpr std::uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

}  // namespace

void Sha256::reset() {
    // Initial hash values: fractional parts of the square roots of the first 8
    // primes (FIPS 180-4, §5.3.3).
    state_[0] = 0x6a09e667;
    state_[1] = 0xbb67ae85;
    state_[2] = 0x3c6ef372;
    state_[3] = 0xa54ff53a;
    state_[4] = 0x510e527f;
    state_[5] = 0x9b05688c;
    state_[6] = 0x1f83d9ab;
    state_[7] = 0x5be0cd19;
    bit_len_ = 0;
    buffer_len_ = 0;
}

void Sha256::process_block(const unsigned char* block) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
               (static_cast<std::uint32_t>(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

    for (int i = 0; i < 64; ++i) {
        std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        std::uint32_t ch = (e & f) ^ ((~e) & g);
        std::uint32_t temp1 = h + S1 + ch + K[i] + w[i];
        std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        std::uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const unsigned char* data, std::size_t len) {
    bit_len_ += static_cast<std::uint64_t>(len) * 8;

    if (buffer_len_ > 0) {
        std::size_t space = 64 - buffer_len_;
        std::size_t copy = (len < space) ? len : space;
        std::memcpy(buffer_ + buffer_len_, data, copy);
        buffer_len_ += copy;
        data += copy;
        len -= copy;
        if (buffer_len_ == 64) {
            process_block(buffer_);
            buffer_len_ = 0;
        }
    }

    while (len >= 64) {
        process_block(data);
        data += 64;
        len -= 64;
    }

    if (len > 0) {
        std::memcpy(buffer_, data, len);
        buffer_len_ = len;
    }
}

std::string Sha256::hex_digest() {
    // Pad: append 0x80, then zeros, leaving room for the 64-bit length.
    std::uint64_t total_bits = bit_len_;
    unsigned char pad = 0x80;
    update(&pad, 1);
    unsigned char zero = 0x00;
    while (buffer_len_ != 56) {
        update(&zero, 1);
    }
    // Append message length as 64-bit big-endian. We must bypass update() here
    // because it would re-count these bytes into bit_len_.
    for (int i = 7; i >= 0; --i) {
        buffer_[buffer_len_++] = static_cast<unsigned char>((total_bits >> (i * 8)) & 0xff);
    }
    process_block(buffer_);
    buffer_len_ = 0;

    static const char* hexchars = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (int i = 0; i < 8; ++i) {
        for (int j = 3; j >= 0; --j) {
            unsigned char byte = static_cast<unsigned char>((state_[i] >> (j * 8)) & 0xff);
            out.push_back(hexchars[byte >> 4]);
            out.push_back(hexchars[byte & 0x0f]);
        }
    }
    return out;
}