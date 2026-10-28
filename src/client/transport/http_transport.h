// The Thrift transport of the client (architecture.md §10): each Thrift
// message is sent in one HTTP POST with libcurl, the way Python's
// THttpClient sends it (server-api.md §1.1, §3.5).

#ifndef CC_CLIENT_TRANSPORT_HTTP_TRANSPORT_H_
#define CC_CLIENT_TRANSPORT_HTTP_TRANSPORT_H_

#include <stddef.h>
#include <stdint.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <thrift/transport/TVirtualTransport.h>

#include "util/result.h"

namespace cc::client::transport {

// A transport for the generated Thrift clients: writes are collected,
// flush() posts them to the URL and the response is what the client reads
// next. Every flush() opens a new connection, as Python does. Not
// thread-safe.
//
// Errors of the HTTP request are thrown as TTransportException (waiver W1
// of cppguide.md), which the call wrapper (client/call/thrift_call.h)
// catches.
class HttpTransport final
    : public apache::thrift::transport::TVirtualTransport<HttpTransport> {
 public:
  // Returns a transport that posts to `url` ("http[s]://host:port/path"),
  // through the proxy that Python's rules choose from the environment
  // (http_proxy, https_proxy, no_proxy, ...). Fails with "Invalid proxy
  // format! ..." if that proxy has no host or no port.
  static Result<std::shared_ptr<HttpTransport>> Create(const std::string& url);

  HttpTransport(const HttpTransport&) = delete;
  HttpTransport& operator=(const HttpTransport&) = delete;

  // Sets the token sent as "Authorization: Bearer <token>"; nullopt sends
  // none.
  void set_token(std::optional<std::string> token) {
    token_ = std::move(token);
  }

  // Sets how long a request may take; nullopt: no limit. A request that
  // takes longer fails with a TTransportException of type TIMED_OUT.
  void set_timeout(std::optional<std::chrono::seconds> timeout) {
    timeout_ = timeout;
  }

  // The connection is opened by flush() and closed after the response.
  bool isOpen() const override { return true; }
  void open() override {}
  void close() override {}

  // Copies the next at most `length` bytes of the response into `buffer`
  // and returns their number.
  uint32_t read(uint8_t* buffer, uint32_t length);

  // Adds `length` bytes of `buffer` to the request.
  void write(const uint8_t* buffer, uint32_t length);

  // Posts the request and receives the response.
  void flush() override;

 private:
  // An HTTP proxy, from the environment.
  struct Proxy {
    std::string host;
    int port = 0;
    // Percent-decoded, as the password.
    std::optional<std::string> user = std::nullopt;
    std::string password = {};
  };

  HttpTransport(std::string url, std::optional<Proxy> proxy)
      : url_(std::move(url)), proxy_(std::move(proxy)) {}

  std::string url_;
  std::optional<Proxy> proxy_;
  std::optional<std::string> token_;
  std::optional<std::chrono::seconds> timeout_;
  std::string request_;
  std::string response_;
  size_t response_position_ = 0;  // The next byte read() returns.
};

}  // namespace cc::client::transport

#endif  // CC_CLIENT_TRANSPORT_HTTP_TRANSPORT_H_
