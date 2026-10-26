#include "server/http/http_server.h"

#include <errno.h>
#include <httplib.h>
#include <signal.h>
#include <stdint.h>
#include <sys/socket.h>

#include <chrono>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "platform/filesystem.h"
#include "platform/process.h"
#include "platform/signal_receiver.h"
#include "platform/socket_options.h"
#include "server/http/api_dispatch.h"
#include "server/http/routing.h"
#include "server/server_context.h"
#include "util/file.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::server::http {
namespace {

constexpr util::Logger kLog("server");

// The content types of the files of the web UI (Python: mimetypes).
struct ContentType {
  const char* extension;
  const char* type;
};

constexpr ContentType kContentTypes[] = {
    {".css", "text/css"},          {".gif", "image/gif"},
    {".gz", "application/gzip"},   {".htm", "text/html"},
    {".html", "text/html"},        {".ico", "image/vnd.microsoft.icon"},
    {".jpeg", "image/jpeg"},       {".jpg", "image/jpeg"},
    {".js", "text/javascript"},    {".json", "application/json"},
    {".mjs", "text/javascript"},   {".png", "image/png"},
    {".svg", "image/svg+xml"},     {".ttf", "font/ttf"},
    {".txt", "text/plain"},        {".wasm", "application/wasm"},
    {".webp", "image/webp"},       {".woff", "font/woff"},
    {".woff2", "font/woff2"},      {".xml", "text/xml"},
};

// The security headers of every GET, HEAD and POST response, based on the
// OWASP Secure Headers Project; they allow no cross-site requests (Python:
// RequestHandler.end_headers()).
void AddSecurityHeaders(httplib::Response& response, bool tls) {
  std::string policy =
      "default-src 'self'; style-src 'unsafe-inline' 'self'; script-src "
      "'unsafe-inline' 'self'; form-action 'self'; frame-ancestors 'none'";
  if (tls) policy += "; upgrade-insecure-requests; block-all-mixed-content";
  response.set_header("X-Frame-Options", "DENY");
  response.set_header("X-XSS-Protection", "1; mode=block");
  response.set_header("X-Content-Type-Options", "nosniff");
  response.set_header("Content-Security-Policy", policy);
  response.set_header("Cross-Origin-Embedder-Policy", "require-corp");
  response.set_header("Cross-Origin-Opener-Policy", "same-origin");
  response.set_header("Cross-Origin-Resource-Policy", "same-origin");
  response.set_header("Referrer-Policy", "no-referrer");
}

// Answers with the error page of Python's send_error(): `code`, `message`
// and the explanation of the code.
void SendError(httplib::Response& response, int code,
               std::string_view message, std::string_view explanation) {
  response.status = code;
  response.set_header("Connection", "close");
  response.set_content(
      std::format("<!DOCTYPE HTML>\n"
                  "<html lang=\"en\">\n"
                  "    <head>\n"
                  "        <meta charset=\"utf-8\">\n"
                  "        <title>Error response</title>\n"
                  "    </head>\n"
                  "    <body>\n"
                  "        <h1>Error response</h1>\n"
                  "        <p>Error code: {}</p>\n"
                  "        <p>Message: {}.</p>\n"
                  "        <p>Error code explanation: {} - {}.</p>\n"
                  "    </body>\n"
                  "</html>\n",
                  code, util::HtmlEscape(message), code,
                  util::HtmlEscape(explanation)),
      "text/html;charset=utf-8");
}

// Returns the file under `root` that the URL path `path` names (Python:
// SimpleHTTPRequestHandler.translate_path()). Parts that would leave
// `root` are dropped.
std::string TranslatePath(const std::string& root, std::string_view path) {
  path = UrlPath(path);
  const bool trailing_slash = util::RightStrip(path).ends_with('/');
  const std::string normalized =
      util::pypath::Normpath(util::PercentDecode(path));
  std::string translated = root;
  for (const std::string& word : util::Split(normalized, "/")) {
    if (word.empty() || word == "." || word == "..") continue;
    translated = util::pypath::Join({translated, word});
  }
  if (trailing_slash) translated += "/";
  return translated;
}

// Returns the content type of the file `path` (Python: guess_type()).
std::string_view ContentTypeOf(const std::string& path) {
  const std::string extension =
      util::AsciiLower(util::pypath::Splitext(path).second);
  for (const ContentType& type : kContentTypes) {
    if (extension == type.extension) return type.type;
  }
  return "application/octet-stream";
}

// Returns `time` as an HTTP date, for example "Mon, 26 Oct 2026 13:00:00
// GMT" (Python: email.utils.formatdate(usegmt=True)).
std::string HttpDate(double time) {
  const std::chrono::sys_seconds point{
      std::chrono::seconds(static_cast<int64_t>(std::floor(time)))};
  return std::format("{:%a, %d %b %Y %H:%M:%S} GMT", point);
}

// Answers with the file of the web UI that `path` names, or with an error
// (Python: SimpleHTTPRequestHandler.send_head()).
void ServeFile(const std::string& root, const std::string& path,
               const httplib::Request& request, httplib::Response& response) {
  std::string file = TranslatePath(root, path);
  if (util::pypath::IsDir(file)) {
    const std::string_view url_path = UrlPath(path);
    if (!url_path.ends_with('/')) {
      response.status = 301;
      std::string location = std::string(url_path) + "/";
      if (const size_t query = path.find('?'); query != std::string::npos) {
        location += path.substr(query);
      }
      response.set_header("Location", location);
      response.set_header("Content-Length", "0");
      return;
    }
    const std::string index = util::pypath::Join({file, "index.html"});
    const std::string htm = util::pypath::Join({file, "index.htm"});
    if (util::pypath::Exists(index)) {
      file = index;
    } else if (util::pypath::Exists(htm)) {
      file = htm;
    } else {
      SendError(response, 405, "No permission to list directory",
                "Specified method is invalid for this resource");
      return;
    }
  }
  const Result<std::string> content =
      file.ends_with('/') ? Result<std::string>(Error{})
                          : util::ReadFile(file);
  if (!content.ok()) {
    SendError(response, 404, "File not found", "Nothing matches the given URI");
    return;
  }
  const std::string last_modified =
      HttpDate(platform::ModificationTime(file).value_or(0.0));
  if (request.get_header_value("If-Modified-Since") == last_modified &&
      !request.has_header("If-None-Match")) {
    response.status = 304;
    return;
  }
  response.status = 200;
  response.set_header("Last-Modified", last_modified);
  response.set_content(content.value(), std::string(ContentTypeOf(file)));
}

// Returns the client of `request` as the log shows it, "<host>:<port>" with
// an IPv6 host in brackets.
std::string ClientAddress(const httplib::Request& request) {
  return std::format("{}:{}", util::BracketIpv6Host(request.remote_addr),
                     request.remote_port);
}

}  // namespace

HttpServer::HttpServer(ServerContext* context, std::string www_root,
                       const std::string& key_file,
                       const std::string& cert_file)
    : context_(context),
      www_root_(std::move(www_root)),
      tls_(util::pypath::IsFile(key_file) && util::pypath::IsFile(cert_file)) {
  if (tls_) {
    server_ = std::make_unique<httplib::SSLServer>(cert_file.c_str(),
                                                   key_file.c_str());
  } else {
    server_ = std::make_unique<httplib::Server>();
  }
  // One request at a time, and the connection is closed after it, as in
  // the Python server (architecture.md §11.1).
  server_->new_task_queue = [] { return new httplib::ThreadPool(1); };
  server_->set_keep_alive_max_count(1);
  // Large store uploads must not be cut off.
  server_->set_payload_max_length(static_cast<size_t>(-1));
  Route();
}

void HttpServer::Route() {
  server_->Get(".*", [this](const httplib::Request& request,
                            httplib::Response& response) {
    kLog.Debug(std::format("{} -- [Anonymous] {} {}", ClientAddress(request),
                           request.method, request.target));
    std::string path = request.target;
    // Python's do_HEAD() sends the file without the rules of do_GET().
    if (request.method == "HEAD") {
      ServeFile(www_root_, path, request, response);
      return;
    }
    if (path == "/") {
      ServeFile(www_root_, "index.html", request, response);
      return;
    }
    if (path == "/live") {
      response.status = 200;
      response.body = "CODECHECKER_SERVER_IS_LIVE";
      return;
    }
    if (path == "/ready") {
      const bool ready =
          context_->config_db()
              .Query("SELECT count(*) FROM server_configurations", {})
              .ok();
      response.status = ready ? 200 : 500;
      response.body = ready ? "CODECHECKER_SERVER_IS_READY"
                            : "CODECHECKER_SERVER_IS_NOT_READY";
      return;
    }
    // A product's pages are the pages of the web UI.
    if (const std::optional<std::string> product = ProductOfPagePath(path)) {
      const size_t at = path.find(*product + "/");
      if (at != std::string::npos) path.erase(at, product->size() + 1);
      // Python: '/' + path.lstrip('/').
      const size_t first = path.find_first_not_of('/');
      path = "/" + (first == std::string::npos ? "" : path.substr(first));
    }
    if (path == "/" || !util::pypath::Exists(TranslatePath(www_root_, path))) {
      path = "index.html";
    }
    ServeFile(www_root_, path, request, response);
  });
  server_->Post(".*", [this](const httplib::Request& request,
                             httplib::Response& response) {
    HandleApiCall(*context_, request, response);
  });
  // No cross-site requests are allowed.
  server_->Options(".*", [](const httplib::Request& request,
                            httplib::Response& response) {
    kLog.Debug(std::format("{} -- [Anonymous] OPTIONS {}",
                           ClientAddress(request), request.target));
    response.status = 403;
  });
  server_->set_post_routing_handler([this](const httplib::Request& request,
                                           httplib::Response& response) {
    if (request.method == "GET" || request.method == "HEAD" ||
        request.method == "POST") {
      AddSecurityHeaders(response, tls_);
    }
    // Removes the deprecated session cookie from browsers.
    if (!response.has_header("X-User") && request.has_header("Cookie")) {
      response.set_header(
          "Set-Cookie",
          std::format("{}=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict",
                      kSessionCookie));
    }
  });
}

Result<int> HttpServer::Bind(const std::string& host, int port) {
  // Python listens on an IPv6 socket if the address has a ':'.
  server_->set_address_family(host.find(':') != std::string::npos ? AF_INET6
                                                                  : AF_INET);
  const ServerConfig& config = context_->config();
  const platform::Keepalive keepalive = {
      .enabled = config.KeepaliveEnabled(),
      .idle = config.KeepaliveIdle(),
      .interval = config.KeepaliveInterval(),
      .max_probe = config.KeepaliveMaxProbe()};
  server_->set_socket_options([keepalive](int fd) {
    platform::SetListeningSocketOptions(fd, keepalive);
  });
  errno = 0;
  int bound = -1;
  if (port == 0) {
    bound = server_->bind_to_any_port(host);
  } else if (server_->bind_to_port(host, port)) {
    bound = port;
  }
  if (bound < 0) {
    const int error = errno;
    return Result<int>(Error{.message = std::format(
                                 "[Errno {}] {}", error,
                                 std::generic_category().message(error))});
  }
  return Result<int>(bound);
}

int HttpServer::Serve() {
  // The listening thread waits for SIGINT; the request threads, which start
  // later, inherit the blocked signal.
  std::error_code error;
  std::optional<platform::SignalReceiver> signals =
      platform::SignalReceiver::Create({SIGINT}, &error);
  if (!signals) {
    kLog.Error(std::format("Couldn't receive signals: {}", error.message()));
    return 1;
  }
  std::jthread waiter([this, &signals](std::stop_token stop) {
    while (!stop.stop_requested()) {
      if (signals->Wait(std::chrono::milliseconds(500))) {
        kLog.Debug(std::format("{}: Received SIGINT (2), performing shutdown "
                               "...",
                               platform::ProcessId()));
        server_->stop();
        return;
      }
    }
  });
  server_->listen_after_bind();
  waiter.request_stop();
  return 128 + SIGINT;
}

}  // namespace cc::server::http
