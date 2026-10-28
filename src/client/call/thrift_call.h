// Port of codechecker_client/thrift_call.py and helpers/base.py: the
// connection to one service of a server, and the wrapper that every API
// call goes through (server-api.md §4.4, §5).

#ifndef CC_CLIENT_CALL_THRIFT_CALL_H_
#define CC_CLIENT_CALL_THRIFT_CALL_H_

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <thrift/protocol/TProtocol.h>

#include "client/transport/http_transport.h"
#include "util/result.h"

namespace cc::client::call {

// Gets a new session token from the server, for a call whose token expired:
// nullopt if the server needs none. Its errors are already reported.
using TokenSource = std::function<Result<std::optional<std::string>>()>;

// The HTTP connection of a service client, and the call wrapper (Python:
// BaseClientHelper and the thrift_client_call decorator). Not thread-safe.
class Connection {
 public:
  // Returns a connection to the service at `url` that sends `token`.
  // `get_new_token`, if set, is asked for a new token once when a call is
  // refused with "Error code 401". Fails with the message of
  // HttpTransport::Create().
  static Result<Connection> Create(const std::string& url,
                                   std::optional<std::string> token,
                                   TokenSource get_new_token);

  Connection(Connection&& other) = default;
  Connection& operator=(Connection&& other) = default;

  // Returns the protocol for the generated Thrift client.
  const std::shared_ptr<apache::thrift::protocol::TProtocol>& protocol()
      const {
    return protocol_;
  }

  // Sets the time limit of the calls (HttpTransport::set_timeout()).
  void set_timeout(std::optional<std::chrono::seconds> timeout) {
    transport_->set_timeout(timeout);
  }

  // Runs `call`, which makes the API call `method` with a generated client
  // on protocol(), and handles its errors like Python's thrift_client_call:
  // a call refused with "Error code 401" is repeated once with a new token;
  // other errors are logged with the reference's messages. The error has
  // an empty message because it is already reported, except when the call
  // took longer than the time limit: then the message says so and nothing
  // is logged.
  Result<void> Run(std::string_view method, const std::function<void()>& call);

 private:
  Connection(std::shared_ptr<transport::HttpTransport> transport,
             TokenSource get_new_token);

  std::shared_ptr<transport::HttpTransport> transport_;
  std::shared_ptr<apache::thrift::protocol::TProtocol> protocol_;
  TokenSource get_new_token_;
};

// A generated Thrift client (`ThriftClient`, for example
// codeCheckerDBAccessClient) on a Connection.
//
//   std::vector<std::string> missing;
//   Result<void> called = results.Call(
//       "getMissingContentHashes", [&](codeCheckerDBAccessClient& client) {
//         client.getMissingContentHashes(missing, hashes);
//       });
template <typename ThriftClient>
class ServiceClient {
 public:
  explicit ServiceClient(Connection connection)
      : connection_(std::move(connection)), client_(connection_.protocol()) {}

  ServiceClient(ServiceClient&& other) = default;
  ServiceClient& operator=(ServiceClient&& other) = default;

  Connection& connection() { return connection_; }

  // Calls `call` with the client through Connection::Run(); `method` is the
  // name of the API method it calls.
  Result<void> Call(std::string_view method,
                    const std::function<void(ThriftClient& client)>& call) {
    return connection_.Run(method, [this, &call] { call(client_); });
  }

 private:
  Connection connection_;
  ThriftClient client_;
};

}  // namespace cc::client::call

#endif  // CC_CLIENT_CALL_THRIFT_CALL_H_
