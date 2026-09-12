#include "transport/http_client.h"

#include <curl/curl.h>

namespace telemetry::transport {
namespace {

// libcurl requires curl_global_init() exactly once before any use and
// curl_global_cleanup() exactly once when the process exits. A function-local
// static gives us both without ordering hazards.
class CurlGlobal {
 public:
  CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CurlGlobal() { curl_global_cleanup(); }
};

void ensure_curl_global() {
  static CurlGlobal global;
  (void)global;
}

size_t write_body(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

class CurlClient final : public HttpClient {
 public:
  CurlClient() { ensure_curl_global(); }
  ~CurlClient() override = default;

  Result post(const std::string& url, const std::string& body, int timeout_ms) override {
    Result r;
    CURL* h = curl_easy_init();
    if (!h) {
      r.error = "curl_easy_init failed";
      return r;
    }

    char errbuf[CURL_ERROR_SIZE] = {0};
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "User-Agent: telemetry-android/1.0");

    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(h, CURLOPT_POST, 1L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms));
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &r.body);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, verify_ ? 1L : 0L);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, verify_ ? 2L : 0L);

    CURLcode rc = curl_easy_perform(h);
    if (rc != CURLE_OK) {
      r.ok = false;
      r.error = errbuf[0] ? errbuf : curl_easy_strerror(rc);
    } else {
      curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &r.http_code);
      r.ok = r.http_code >= 200 && r.http_code < 300;
      if (!r.ok) r.error = "http " + std::to_string(r.http_code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(h);
    return r;
  }

  void set_verify(bool verify) override { verify_ = verify; }

 private:
  bool verify_ = true;
};

}  // namespace

std::unique_ptr<HttpClient> make_default_http_client() {
  return std::make_unique<CurlClient>();
}

}  // namespace telemetry::transport