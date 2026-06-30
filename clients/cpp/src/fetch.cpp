#include "fetch.hpp"

#include <chrono>

#include "process.hpp"

namespace {

using Clock = std::chrono::steady_clock;

inline std::int64_t ms_since(Clock::time_point start) {
    auto d = Clock::now() - start;
    return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

// libcurl write callback. Streams each received chunk straight into the
// StreamProcessor (hash + counts) without buffering the whole payload. This is
// the C++ analog of handing the response body to io.Copy(multiWriter, body) in
// Go. `userdata` is the StreamProcessor for this request.
size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t n = size * nmemb;
    auto* proc = static_cast<StreamProcessor*>(userdata);
    proc->consume(reinterpret_cast<const unsigned char*>(ptr), n);
    return n;  // returning < n signals an error to libcurl
}

Record error_record(const std::string& target, std::int64_t fetch_ms,
                    std::string reason) {
    Record r;
    r.target = target;
    r.status = "error";
    r.size_bytes = 0;
    r.line_count = 0;
    r.sha256 = "";
    r.fetch_ms = fetch_ms;
    r.error = std::move(reason);
    return r;
}

}  // namespace

Record fetch_and_process(CURL* handle,
                         const std::string& base_url,
                         const std::string& target,
                         long timeout_ms) {
    auto start = Clock::now();

    std::string url;
    url.reserve(base_url.size() + 9 + target.size());
    url = base_url;
    url += "/payload/";
    url += target;
    StreamProcessor proc;

    // Reset per-request options but keep the connection cache associated with
    // the handle (reusing the handle is what pools connections across requests).
    curl_easy_reset(handle);
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &proc);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);  // thread-safe timeouts
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
    // Do NOT set CURLOPT_ACCEPT_ENCODING: omitting it sends no Accept-Encoding
    // header, so the server returns raw bytes and curl performs no decompression.
    // Setting it to "" requests all supported encodings and enables transparent
    // decompression, which would silently corrupt byte counts and SHA-256.

    CURLcode rc = curl_easy_perform(handle);
    std::int64_t fetch_ms = ms_since(start);

    if (rc != CURLE_OK) {
        return error_record(target, fetch_ms,
                            std::string("fetch: ") + curl_easy_strerror(rc));
    }

    long http_code = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code < 200 || http_code >= 300) {
        return error_record(target, fetch_ms,
                            "status " + std::to_string(http_code));
    }

    Record r;
    r.target = target;
    r.status = "ok";
    r.sha256 = proc.sha_hex();
    r.size_bytes = proc.size_bytes();
    r.line_count = proc.line_count();
    r.fetch_ms = fetch_ms;
    return r;
}