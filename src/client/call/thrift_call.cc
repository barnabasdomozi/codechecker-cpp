#include "client/call/thrift_call.h"

#include <exception>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <thrift/TApplicationException.h>
#include <thrift/Thrift.h>
#include <thrift/protocol/TJSONProtocol.h>
#include <thrift/protocol/TProtocolException.h>
#include <thrift/transport/TTransportException.h>

#include "api/thrift/shared_types.h"
#include "client/transport/http_transport.h"
#include "util/logging.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::client::call {
namespace {

using apache::thrift::TApplicationException;
using apache::thrift::TException;
using apache::thrift::protocol::TProtocolException;
using apache::thrift::transport::TTransportException;

constexpr util::Logger kLog("system");

// Logs the RequestFailed that the call `method` got, with the reference's
// messages by error code.
void LogRequestFailed(std::string_view method, const ::RequestFailed& failed) {
  kLog.Error(std::format("Calling API endpoint: {}", method));
  // The reference passes on a GENERAL error whose first extra information
  // is "report_limit" to the caller, but no server sends one.
  switch (failed.errorCode) {
    case ::ErrorCode::DATABASE:
      kLog.Error("Database error on server\n" + failed.message);
      break;
    case ::ErrorCode::AUTH_DENIED:
      kLog.Error("Authentication denied\n " + failed.message);
      break;
    case ::ErrorCode::UNAUTHORIZED:
      kLog.Error("Unauthorized to access\n " + failed.message);
      kLog.Error("Ask the product admin for additional access rights.");
      break;
    case ::ErrorCode::API_MISMATCH:
      kLog.Error("Client/server API mismatch\n " + failed.message);
      break;
    default:
      kLog.Error(std::format("Error during API call: {}", method));
      kLog.Error(failed.message);
      if (!failed.extraInfo.empty()) {
        kLog.Error(util::Join(failed.extraInfo, "\n"));
      }
  }
}

// Logs the protocol error `error` of the call `method`. The reference also
// logs the arguments of the call, which are not known here.
void LogProtocolError(std::string_view method,
                      const TProtocolException& error) {
  switch (error.getType()) {
    case TProtocolException::UNKNOWN:
      kLog.Error("Unknown thrift error");
      break;
    case TProtocolException::INVALID_DATA:
      kLog.Error("Thrift invalid data error.");
      break;
    case TProtocolException::NEGATIVE_SIZE:
      kLog.Error("Thrift negative size error.");
      break;
    case TProtocolException::SIZE_LIMIT:
      kLog.Error("Thrift size limit error.");
      break;
    case TProtocolException::BAD_VERSION:
      kLog.Error("Thrift bad version error.");
      break;
    default:
      break;
  }
  kLog.Error(std::string(method));
  kLog.Error(std::format("Request failed.\n{}", error.what()));
}

// Logs that the server cannot be reached because of `message`.
void LogConnectionFailed(std::string_view message) {
  kLog.Error("Connection failed.");
  kLog.Error(std::format("OS Error: {}", message));
  kLog.Error("Check if your CodeChecker server is running.");
}

}  // namespace

Connection::Connection(std::shared_ptr<transport::HttpTransport> transport,
                       TokenSource get_new_token)
    : transport_(std::move(transport)),
      protocol_(std::make_shared<apache::thrift::protocol::TJSONProtocol>(
          transport_)),
      get_new_token_(std::move(get_new_token)) {}

Result<Connection> Connection::Create(const std::string& url,
                                      std::optional<std::string> token,
                                      TokenSource get_new_token) {
  Result<std::shared_ptr<transport::HttpTransport>> transport =
      transport::HttpTransport::Create(url);
  if (!transport.ok()) return Result<Connection>(transport.error());
  transport.value()->set_token(std::move(token));
  return Result<Connection>(
      Connection(std::move(transport).value(), std::move(get_new_token)));
}

Result<void> Connection::Run(std::string_view method,
                             const std::function<void()>& call) {
  // The only place where the client catches exceptions: those of the
  // generated Thrift code and of the transport (waiver W1 of cppguide.md).
  try {
    try {
      call();
      return Result<void>();
    } catch (const TApplicationException& error) {
      // An expired session: get a new token and try once more.
      if (std::string_view(error.what()).find("Error code 401") ==
          std::string_view::npos) {
        throw;
      }
      if (get_new_token_) {
        Result<std::optional<std::string>> token = get_new_token_();
        if (!token.ok()) return Result<void>(token.error());
        if (token.value()) transport_->set_token(token.value());
      }
      call();
      return Result<void>();
    }
  } catch (const ::RequestFailed& failed) {
    LogRequestFailed(method, failed);
  } catch (const TApplicationException& error) {
    kLog.Error(std::format("Internal server error: {}", error.what()));
  } catch (const TProtocolException& error) {
    LogProtocolError(method, error);
  } catch (const TTransportException& error) {
    if (error.getType() == TTransportException::TIMED_OUT) {
      return Result<void>(Error{.message = error.what()});
    }
    LogConnectionFailed(error.what());
  } catch (const TException& error) {
    LogConnectionFailed(error.what());
  } catch (const std::exception& error) {
    LogConnectionFailed(error.what());
  }
  return Result<void>(Error{});
}

}  // namespace cc::client::call
