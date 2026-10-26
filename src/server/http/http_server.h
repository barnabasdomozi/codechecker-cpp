// Port of CCSimpleHttpServer and RequestHandler of
// codechecker_server/server.py: the HTTP server of the API processes, which
// serves the web UI files and the Thrift API (server-api.md §2,
// architecture.md §11.3).

#ifndef CC_SERVER_HTTP_HTTP_SERVER_H_
#define CC_SERVER_HTTP_HTTP_SERVER_H_

#include <httplib.h>

#include <memory>
#include <string>

#include "server/server_context.h"
#include "util/result.h"

namespace cc::server::http {

// The HTTP server. The supervisor creates it and binds its socket; each API
// process inherits it and serves requests on that socket, one at a time,
// closing the connection after each response, as the Python server does.
class HttpServer {
 public:
  // Sets up a server for `context` that serves the files of `www_root`. If
  // `key_file` and `cert_file` exist, it uses TLS. Keeps `context`, which
  // must outlive it.
  HttpServer(ServerContext* context, std::string www_root,
             const std::string& key_file, const std::string& cert_file);

  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

  // Binds the listening socket to `host` and `port` (0: a free port), with
  // the keepalive settings of the server configuration. Returns the port.
  // The error message is the socket error, like "[Errno 98] Address
  // already in use".
  Result<int> Bind(const std::string& host, int port);

  // Returns whether the server uses TLS.
  bool tls() const { return tls_; }

  // Serves requests until the process receives SIGINT, then closes the
  // socket and returns the exit code of an API process, 128 + SIGINT; 1 if
  // it cannot receive signals. Call it in an API process after Bind().
  int Serve();

 private:
  // Sets up the request handlers.
  void Route();

  ServerContext* context_;
  std::string www_root_;
  bool tls_;
  std::unique_ptr<httplib::Server> server_;
};

}  // namespace cc::server::http

#endif  // CC_SERVER_HTTP_HTTP_SERVER_H_
