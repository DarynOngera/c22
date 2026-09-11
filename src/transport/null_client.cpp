#include "transport/http_client.h"

// Non-Android build: no background TLS stack available; provide a stub that
// reports failure so host tests exercise the retry path.
namespace telemetry::transport {

namespace {
class NullClient final : public HttpClient {
 public:
  Result post(const std::string&, const std::string&, int) override {
    Result r;
    r.ok = false;
    r.error = "transport stub (no curl on host)";
    return r;
  }
  void set_verify(bool) override {}
};
}  // namespace

std::unique_ptr<HttpClient> make_default_http_client() {
  return std::make_unique<NullClient>();
}

}  // namespace telemetry::transport