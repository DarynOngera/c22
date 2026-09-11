#pragma once

#include <memory>
#include <string>

namespace telemetry::transport {

// Minimal synchronous HTTP/HTTPS client used by the telemetry sink.
class HttpClient {
 public:
  struct Result {
    bool ok = false;
    long http_code = 0;
    std::string body;
    std::string error;
  };

  virtual ~HttpClient() = default;
  virtual Result post(const std::string& url, const std::string& body, int timeout_ms) = 0;
  virtual void set_verify(bool verify) = 0;
};

// Default client: libcurl+OpenSSL on Android, a local no-op on other platforms.
std::unique_ptr<HttpClient> make_default_http_client();

}  // namespace telemetry::transport