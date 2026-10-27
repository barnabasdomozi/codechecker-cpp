#include "server/http/api_dispatch.h"

#include <httplib.h>
#include <stdint.h>

#include <algorithm>
#include <exception>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <thrift/TApplicationException.h>
#include <thrift/Thrift.h>
#include <thrift/protocol/TJSONProtocol.h>
#include <thrift/protocol/TProtocol.h>
#include <thrift/transport/TBufferTransports.h>

#include "api/thrift/codeCheckerAuthentication.h"
#include "api/thrift/codeCheckerDBAccess.h"
#include "api/thrift/codeCheckerProductService.h"
#include "api/thrift/configurationService.h"
#include "api/thrift/serverInfoService.h"
#include "api/thrift/shared_types.h"
#include "server/api/authentication_handler.h"
#include "server/api/configuration_handler.h"
#include "server/api/product_handler.h"
#include "server/api/report_handler.h"
#include "server/api/server_info_handler.h"
#include "server/auth/session.h"
#include "server/http/routing.h"
#include "server/product.h"
#include "server/server_context.h"
#include "util/logging.h"
#include "util/strings.h"

namespace cc::server::http {
namespace {

using apache::thrift::TApplicationException;
using apache::thrift::TException;
using apache::thrift::protocol::TJSONProtocol;
using apache::thrift::protocol::TMessageType;
using apache::thrift::protocol::TType;
using apache::thrift::transport::TMemoryBuffer;

constexpr util::Logger kLog("server");

// The method name and sequence id at the start of a Thrift message.
struct CallHeader {
  std::string method;
  int32_t seqid = 0;
};

// Returns a Thrift JSON protocol that reads `body`.
std::shared_ptr<TJSONProtocol> ReadProtocol(const std::string& body) {
  // TMemoryBuffer only reads the data; COPY makes it own a copy.
  auto buffer = std::make_shared<TMemoryBuffer>(
      reinterpret_cast<uint8_t*>(const_cast<char*>(body.data())),
      static_cast<uint32_t>(body.size()), TMemoryBuffer::COPY);
  return std::make_shared<TJSONProtocol>(buffer);
}

// Returns the method name and sequence id of the message `body`; nullopt
// if it is not a Thrift JSON message.
std::optional<CallHeader> ReadCallHeader(const std::string& body) {
  CallHeader header;
  TMessageType type = apache::thrift::protocol::T_CALL;
  try {
    ReadProtocol(body)->readMessageBegin(header.method, type, header.seqid);
  } catch (const TException&) {
    return std::nullopt;
  }
  return header;
}

// Returns the reply to the call `header` that reports the error `message`
// as a TApplicationException of type INTERNAL_ERROR (Python:
// send_thrift_exception()).
std::string ExceptionReply(const CallHeader& header,
                           const std::string& message) {
  auto buffer = std::make_shared<TMemoryBuffer>();
  TJSONProtocol protocol(buffer);
  const TApplicationException error(TApplicationException::INTERNAL_ERROR,
                                    message);
  protocol.writeMessageBegin(header.method,
                             apache::thrift::protocol::T_EXCEPTION,
                             header.seqid);
  error.write(&protocol);
  protocol.writeMessageEnd();
  protocol.getTransport()->flush();
  return buffer->getBufferAsString();
}

// Returns the live session that `request` presents: a Bearer token, or the
// deprecated session cookie; null if there is none or authentication is
// off (Python: __check_session_header()).
const auth::Session* SessionOf(ServerContext& context,
                               const httplib::Request& request) {
  auth::SessionManager& sessions = context.sessions();
  if (!sessions.enabled()) return nullptr;
  const auth::Session* session = nullptr;
  const std::string authorization =
      request.get_header_value("Authorization");
  if (authorization.starts_with("Bearer ")) {
    session = sessions.GetSession(authorization.substr(7));
  }
  if (session == nullptr && request.has_header("Cookie")) {
    for (const std::string& cookie :
         util::Split(request.get_header_value("Cookie"), "; ")) {
      const std::vector<std::string> parts = util::Split(cookie, "=");
      if (parts.size() == 2 && parts[0] == kSessionCookie) {
        session = sessions.GetSession(parts[1]);
      }
    }
  }
  return session;
}

// Returns the product with `endpoint` if its database can be used,
// reconnecting to it if it was not connected; otherwise null, with the
// error message in `*error` (Python: __check_prod_db()).
Product* CheckProductDatabase(ServerContext& context,
                              const std::string& endpoint,
                              std::string* error) {
  Product* product = context.products().Get(endpoint);
  if (product == nullptr) {
    *error = std::format(
        "The product with the given endpoint '{}' does not exist!", endpoint);
    return nullptr;
  }
  const DBStatus::type status = product->db_status();
  if (status == DBStatus::OK) return product;
  if (status == DBStatus::FAILED_TO_CONNECT || status == DBStatus::MISSING ||
      status == DBStatus::SCHEMA_INIT_ERROR) {
    kLog.Error(std::format(
        "Request's product '{}' is not connected! Attempting reconnect...",
        endpoint));
    product->Connect(/*init=*/false);
    if (product->db_status() == DBStatus::OK) return product;
    kLog.Debug("Product reconnection failed.");
    *error = std::format("'{}' database connection failed!", endpoint);
  } else {
    *error = std::format("'{}' database connection failed. DB status: {}",
                         endpoint, _DBStatus_VALUES_TO_NAMES.at(status));
  }
  kLog.Error(*error);
  return nullptr;
}

// Runs the call `body` with a `Processor` of a new `Handler` for `request`
// and returns the reply.
template <typename Processor, typename Handler>
std::string Process(const Request& request, const std::string& body) {
  Processor processor(std::make_shared<Handler>(request));
  auto output = std::make_shared<TMemoryBuffer>();
  auto protocol = std::make_shared<TJSONProtocol>(output);
  processor.process(ReadProtocol(body), protocol, nullptr);
  return output->getBufferAsString();
}

// Logs the RequestFailed that the reply `reply` of the CodeCheckerService
// call `method` carries, as "<method>:\n<message>" (Python: the
// exc_to_thrift_reqfail decorator of the methods of report_server.py).
void LogRequestFailed(const std::string& method, const std::string& reply) {
  // The methods that the reference does not decorate.
  constexpr std::string_view kUndecorated[] = {
      "getCheckCommand", "getRunReportCounts", "getPackageVersion",
      "removeRunResults"};
  if (std::ranges::find(kUndecorated, method) != std::end(kUndecorated)) {
    return;
  }
  // A reply is a struct whose field 0 is the result and field 1 the
  // RequestFailed of the method. The processor has just written `reply`, so
  // it reads without errors; anything Thrift still throws is caught by
  // HandleApiCall(), as for the call itself.
  const std::shared_ptr<TJSONProtocol> protocol = ReadProtocol(reply);
  std::string name;
  TMessageType type = apache::thrift::protocol::T_REPLY;
  int32_t seqid = 0;
  protocol->readMessageBegin(name, type, seqid);
  if (type != apache::thrift::protocol::T_REPLY) return;
  protocol->readStructBegin(name);
  TType field_type = apache::thrift::protocol::T_STOP;
  int16_t field_id = 0;
  protocol->readFieldBegin(name, field_type, field_id);
  if (field_id != 1 || field_type != apache::thrift::protocol::T_STRUCT) {
    return;
  }
  ::RequestFailed failed;
  failed.read(protocol.get());
  kLog.Warning(std::format("{}:\n{}", method, failed.message));
}

// Returns the reply of the call `body` of `method` to `service`; nullopt if
// the server has no such service, with the reason in `*error`. `method` is
// only used to log the errors of CodeCheckerService calls.
std::optional<std::string> CallService(const Request& request,
                                       const std::string& service,
                                       const std::string& method,
                                       const std::string& body,
                                       std::string* error) {
  if (service == "Authentication") {
    return Process<codeCheckerAuthenticationProcessor,
                   api::AuthenticationHandler>(request, body);
  }
  if (service == "Configuration") {
    return Process<configurationServiceProcessor, api::ConfigurationHandler>(
        request, body);
  }
  if (service == "ServerInfo") {
    return Process<serverInfoServiceProcessor, api::ServerInfoHandler>(request,
                                                                      body);
  }
  if (service == "Products") {
    return Process<codeCheckerProductServiceProcessor, api::ProductHandler>(
        request, body);
  }
  if (service == "CodeCheckerService" && request.product == nullptr) {
    *error = "Requested CodeCheckerService on a nonexistent product: 'None'.";
    return std::nullopt;
  }
  if (service == "CodeCheckerService") {
    std::string reply =
        Process<codeCheckerDBAccessProcessor, api::ReportHandler>(request,
                                                                  body);
    LogRequestFailed(method, reply);
    return reply;
  }
  if (service == "Tasks") {
    *error = std::format(
        "The '{}' API is not supported by this server yet.", service);
    return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace

void HandleApiCall(ServerContext& context, const httplib::Request& request,
                   httplib::Response& response) {
  const std::optional<CallHeader> header = ReadCallHeader(request.body);
  if (!header) {
    response.status = 400;
    return;
  }
  const auth::Session* session = SessionOf(context, request);
  const std::string host = util::BracketIpv6Host(request.remote_addr);
  const std::string api_info = std::format(
      "{}:{} -- [{}] POST {}@{}", host, request.remote_port,
      session ? session->user : "Anonymous", request.target, header->method);
  kLog.Info(api_info);
  if (session != nullptr && !session->user.empty()) {
    response.set_header("X-User", session->user);
  }
  auto reply = [&response](const std::string& body) {
    response.status = 200;
    response.set_content(body, "application/x-thrift");
  };

  const std::optional<ApiRoute> route = SplitApiPath(request.target);
  if (!route) {
    reply(ExceptionReply(*header, "Invalid request endpoint path."));
    return;
  }
  if (context.sessions().enabled() && session == nullptr &&
      route->service != "Authentication" &&
      route->service != "Configuration" && route->service != "ServerInfo") {
    kLog.Debug(std::format(
        "{}:{} Invalid access, credentials not found - session refused.",
        host, request.remote_port));
    reply(ExceptionReply(*header, "Error code 401: Unauthorized!"));
    return;
  }

  std::string error;
  Product* product = nullptr;
  if (route->product) {
    product = CheckProductDatabase(context, *route->product, &error);
    if (product == nullptr) {
      kLog.Debug(std::format("{} failed with Exception: {}", api_info, error));
      reply(ExceptionReply(*header, error));
      return;
    }
  }
  const Request api_request = {.server = &context,
                               .session = session,
                               .product = product,
                               .api_version = route->version};
  try {
    std::optional<std::string> result =
        CallService(api_request, route->service, header->method,
                    request.body, &error);
    if (!result) {
      if (error.empty()) {
        error = std::format("No API endpoint named '{}'.", request.target);
      }
      kLog.Debug(std::format("{} failed with Exception: {}", api_info, error));
      reply(ExceptionReply(*header, error));
      return;
    }
    reply(*result);
  } catch (const TException& exception) {
    kLog.Warning(std::format("{} failed with Exception: {}", api_info,
                             exception.what()));
    reply(ExceptionReply(*header, exception.what()));
  } catch (const std::exception& exception) {
    kLog.Warning(std::format("{} failed with Exception: {}", api_info,
                             exception.what()));
    reply(ExceptionReply(*header, exception.what()));
  }
}

}  // namespace cc::server::http
