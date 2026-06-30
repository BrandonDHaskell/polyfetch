#include "process.hpp"

#include <algorithm>

void StreamProcessor::consume(const unsigned char* data, std::size_t len) {
    sha_.update(data, len);
    size_bytes_ += static_cast<std::int64_t>(len);
    line_count_ += std::count(data, data + len, static_cast<unsigned char>('\n'));
}
