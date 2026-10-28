#include "client/transport/http_transport.h"

#include <curl/curl.h>
#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <thrift/transport/TTransportException.h>

#include "common/package.h"
#include "platform/subprocess.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::client::transport {
namespace {

using apache::thrift::transport::TTransportException;

// The parts of a URL that the proxy rules need (Python: the urlparse()
// result).
struct ParsedUrl {
  std::string scheme;  // Lower case; empty if there is none.
  std::string netloc;  // The part after "//", up to the path.
};

// Splits `url` like Python's urllib.parse.urlsplit(): a scheme is letters,
// digits, "+", "-" and "." before the first ":", starting with a letter; a
// netloc follows only after "//".
ParsedUrl ParseUrl(std::string_view url) {
  const auto is_letter = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  };
  ParsedUrl parsed;
  const size_t colon = url.find(':');
  if (colon != std::string_view::npos && colon > 0 && is_letter(url[0]) &&
      std::ranges::all_of(url.substr(0, colon), [&is_letter](char c) {
        return is_letter(c) || (c >= '0' && c <= '9') || c == '+' ||
               c == '-' || c == '.';
      })) {
    parsed.scheme = util::AsciiLower(url.substr(0, colon));
    url.remove_prefix(colon + 1);
  }
  if (url.starts_with("//")) {
    url.remove_prefix(2);
    parsed.netloc = std::string(url.substr(0, url.find_first_of("/?#")));
  }
  return parsed;
}

// Returns whether `text` is not empty and has only the digits 0-9.
bool IsDigits(std::string_view text) {
  return !text.empty() && std::ranges::all_of(text, [](char c) {
    return c >= '0' && c <= '9';
  });
}

// The host, port and user of a netloc, "[user[:password]@]host[:port]".
struct NetworkLocation {
  std::string host;  // Lower case, without brackets.
  // nullopt: no port; an empty string inside: not a valid port (Python
  // raises ValueError).
  std::optional<std::string> port;
  std::optional<std::string> user;
  std::optional<std::string> password;
};

// Splits `netloc` like the hostname, port, username and password
// attributes of Python's urlparse() result.
NetworkLocation SplitNetloc(std::string_view netloc) {
  NetworkLocation location;
  if (const size_t at = netloc.rfind('@'); at != std::string_view::npos) {
    const std::string_view user_info = netloc.substr(0, at);
    const size_t colon = user_info.find(':');
    location.user = std::string(user_info.substr(0, colon));
    if (colon != std::string_view::npos) {
      location.password = std::string(user_info.substr(colon + 1));
    }
    netloc.remove_prefix(at + 1);
  }
  std::string_view port;
  bool has_port = false;
  if (netloc.starts_with('[')) {
    const size_t end = netloc.find(']');
    location.host = std::string(netloc.substr(1, end - 1));
    if (end != std::string_view::npos) {
      const std::string_view after = netloc.substr(end + 1);
      has_port = after.starts_with(':');
      if (has_port) port = after.substr(1);
    }
  } else {
    const size_t colon = netloc.find(':');
    location.host = std::string(netloc.substr(0, colon));
    has_port = colon != std::string_view::npos;
    if (has_port) port = netloc.substr(colon + 1);
  }
  location.host = util::AsciiLower(location.host);
  if (has_port && !port.empty()) {
    const std::optional<int64_t> number = util::ParseInt(port);
    location.port =
        IsDigits(port) && number && *number <= 65535 ? std::string(port) : "";
  }
  return location;
}

// Returns the proxies of the environment by scheme, and "no" for no_proxy
// (Python: urllib.request.getproxies_environment()). Lower case names win
// over others.
std::map<std::string, std::string> ProxiesOfEnvironment() {
  std::map<std::string, std::string> proxies;
  const std::vector<std::string> environment = platform::CurrentEnvironment();
  for (const std::string& entry : environment) {
    const size_t equals = entry.find('=');
    const std::string name = util::AsciiLower(entry.substr(0, equals));
    const std::string value = entry.substr(equals + 1);
    if (!value.empty() && name.ends_with("_proxy")) {
      proxies[name.substr(0, name.size() - 6)] = value;
    }
  }
  // A CGI script must not take HTTP_PROXY from the request (CVE-2016-1000110).
  if (platform::GetEnv("REQUEST_METHOD")) proxies.erase("http");
  for (const std::string& entry : environment) {
    const size_t equals = entry.find('=');
    const std::string name = entry.substr(0, equals);
    if (!name.ends_with("_proxy")) continue;
    const std::string key = util::AsciiLower(name.substr(0, name.size() - 6));
    if (equals + 1 < entry.size()) {
      proxies[key] = entry.substr(equals + 1);
    } else {
      proxies.erase(key);
    }
  }
  return proxies;
}

// Returns whether no_proxy (`no_proxy`) says that `host` is reached without
// a proxy (Python: urllib.request.proxy_bypass_environment()).
bool BypassesProxy(std::string_view no_proxy, std::string_view host) {
  if (no_proxy == "*") return true;
  const std::string lower = util::AsciiLower(host);
  // Python: _splitport().
  std::string host_only = lower;
  if (const size_t colon = lower.rfind(':'); colon != std::string::npos) {
    if (IsDigits(std::string_view(lower).substr(colon + 1))) {
      host_only = lower.substr(0, colon);
    }
  }
  for (const std::string& entry : util::Split(no_proxy, ",")) {
    const std::string_view name = util::Strip(entry);
    if (name.empty()) continue;
    const std::string domain = util::AsciiLower(util::LeftStrip(name, "."));
    if (host_only == domain || lower == domain) return true;
    if (host_only.ends_with("." + domain) || lower.ends_with("." + domain)) {
      return true;
    }
  }
  return false;
}

// Returns the text of Python's OSError for a failed connection, for example
// "[Errno 111] Connection refused".
std::string ConnectionErrorText(CURL* curl, CURLcode code,
                                std::string_view message) {
  if (code == CURLE_COULDNT_RESOLVE_HOST) {
    // Python: socket.gaierror of getaddrinfo().
    return "[Errno -2] Name or service not known";
  }
  if (code == CURLE_PEER_FAILED_VERIFICATION) {
    constexpr std::string_view kProblem = "SSL certificate problem: ";
    if (message.starts_with(kProblem)) message.remove_prefix(kProblem.size());
    return std::format("[SSL: CERTIFICATE_VERIFY_FAILED] certificate verify "
                       "failed: {}",
                       message);
  }
  long os_error = 0;  // libcurl writes a long.
  if (code == CURLE_COULDNT_CONNECT &&
      curl_easy_getinfo(curl, CURLINFO_OS_ERRNO, &os_error) == CURLE_OK &&
      os_error != 0) {
    const int error_number = static_cast<int>(os_error);
    return std::format("[Errno {}] {}", error_number,
                       std::generic_category().message(error_number));
  }
  return message.empty() ? std::string(curl_easy_strerror(code))
                         : std::string(message);
}

// Appends the bytes libcurl received to the std::string `userdata`.
size_t AppendResponse(char* data, size_t size, size_t count, void* userdata) {
  static_cast<std::string*>(userdata)->append(data, size * count);
  return size * count;
}

// Frees libcurl handles.
struct CurlDeleter {
  void operator()(CURL* curl) const { curl_easy_cleanup(curl); }
  void operator()(curl_slist* list) const { curl_slist_free_all(list); }
};

// Returns the header lines of a request; `token`, if set, is sent as a
// bearer token.
std::unique_ptr<curl_slist, CurlDeleter> RequestHeaders(
    const std::optional<std::string>& token) {
  std::vector<std::string> lines = {
      "Content-Type: application/x-thrift",
      std::format("User-Agent: {}", common::kProgramName),
      // libcurl would wait for "100 Continue" before large bodies, which
      // the Python server never sends.
      "Expect:"};
  if (token) lines.push_back("Authorization: Bearer " + *token);
  std::unique_ptr<curl_slist, CurlDeleter> headers;
  for (const std::string& line : lines) {
    headers.reset(curl_slist_append(headers.release(), line.c_str()));
  }
  return headers;
}

// Makes `curl` trust the certificates that Python's ssl module takes from
// these OpenSSL variables; libcurl only takes those of the bundle it was
// built with.
void SetTrustedCertificates(CURL* curl) {
  const std::optional<std::string> ca_file = platform::GetEnv("SSL_CERT_FILE");
  if (ca_file) curl_easy_setopt(curl, CURLOPT_CAINFO, ca_file->c_str());
  const std::optional<std::string> ca_dir = platform::GetEnv("SSL_CERT_DIR");
  if (ca_dir) curl_easy_setopt(curl, CURLOPT_CAPATH, ca_dir->c_str());
}

// Throws the TTransportException of the request of `curl` that failed with
// `code` and the libcurl error `message`: TIMED_OUT after the time limit,
// NOT_OPEN with the text of Python's OSError otherwise.
[[noreturn]] void ThrowRequestError(CURL* curl, CURLcode code,
                                    std::string_view message) {
  if (code == CURLE_OPERATION_TIMEDOUT) {
    throw TTransportException(TTransportException::TIMED_OUT,
                              std::string(message));
  }
  throw TTransportException(TTransportException::NOT_OPEN,
                            ConnectionErrorText(curl, code, message));
}

}  // namespace

Result<std::shared_ptr<HttpTransport>> HttpTransport::Create(
    const std::string& url) {
  const ParsedUrl target = ParseUrl(url);
  const std::string host = SplitNetloc(target.netloc).host;
  std::map<std::string, std::string> proxies = ProxiesOfEnvironment();
  std::optional<Proxy> proxy;
  const auto chosen = proxies.find(target.scheme);
  const auto no_proxy = proxies.find("no");
  if (chosen != proxies.end() &&
      (no_proxy == proxies.end() || !BypassesProxy(no_proxy->second, host))) {
    const NetworkLocation location =
        SplitNetloc(ParseUrl(chosen->second).netloc);
    if (location.host.empty() || !location.port || location.port->empty()) {
      return Result<std::shared_ptr<HttpTransport>>(Error{
          .message = "Invalid proxy format! Check your HTTP_PROXY/HTTPS_PROXY "
                     "environment variables if these are in the right "
                     "format: 'http[s]://host:port'."});
    }
    proxy = Proxy{.host = location.host,
                  .port = static_cast<int>(*util::ParseInt(*location.port))};
    if (location.user && !location.user->empty()) {
      proxy->user = util::PercentDecode(*location.user);
      proxy->password = util::PercentDecode(location.password.value_or(""));
    }
  }
  return Result<std::shared_ptr<HttpTransport>>(std::shared_ptr<HttpTransport>(
      new HttpTransport(url, std::move(proxy))));
}

uint32_t HttpTransport::read(uint8_t* buffer, uint32_t length) {
  const size_t count =
      std::min<size_t>(length, response_.size() - response_position_);
  std::memcpy(buffer, response_.data() + response_position_, count);
  response_position_ += count;
  return static_cast<uint32_t>(count);
}

void HttpTransport::write(const uint8_t* buffer, uint32_t length) {
  request_.append(reinterpret_cast<const char*>(buffer), length);
}

void HttpTransport::flush() {
  const std::string request = std::move(request_);
  request_.clear();
  response_.clear();
  response_position_ = 0;

  const std::unique_ptr<CURL, CurlDeleter> curl(curl_easy_init());
  if (!curl) {
    throw TTransportException(TTransportException::UNKNOWN,
                              "Cannot initialize libcurl.");
  }
  const std::unique_ptr<curl_slist, CurlDeleter> headers =
      RequestHeaders(token_);
  char error_buffer[CURL_ERROR_SIZE] = {'\0'};
  CURL* handle = curl.get();
  curl_easy_setopt(handle, CURLOPT_URL, url_.c_str());
  curl_easy_setopt(handle, CURLOPT_POST, 1L);
  curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.data());
  curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE,
                   static_cast<curl_off_t>(request.size()));
  curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &AppendResponse);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response_);
  curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, error_buffer);
  curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
  // The proxy was chosen by Python's rules; "" keeps libcurl from choosing
  // one from the environment by its own rules.
  const std::string proxy =
      proxy_ ? std::format("http://{}:{}", util::BracketIpv6Host(proxy_->host),
                           proxy_->port)
             : "";
  curl_easy_setopt(handle, CURLOPT_PROXY, proxy.c_str());
  if (proxy_ && proxy_->user) {
    curl_easy_setopt(handle, CURLOPT_PROXYUSERNAME, proxy_->user->c_str());
    curl_easy_setopt(handle, CURLOPT_PROXYPASSWORD, proxy_->password.c_str());
  }
  SetTrustedCertificates(handle);
  if (timeout_) {
    // libcurl takes a long.
    curl_easy_setopt(handle, CURLOPT_TIMEOUT,
                     static_cast<long>(timeout_->count()));
  }

  const CURLcode code = curl_easy_perform(handle);
  if (code == CURLE_OK) return;
  response_.clear();
  ThrowRequestError(handle, code, error_buffer);
}

}  // namespace cc::client::transport
