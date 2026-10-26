// Port of RequestHandler.do_POST() of codechecker_server/server.py: one
// Thrift API call over HTTP POST (server-api.md §1.1, §2.2). This is where
// the server meets Thrift, so it catches the exceptions of the Thrift
// processors and turns them into replies (cppguide.md W1).

#ifndef CC_SERVER_HTTP_API_DISPATCH_H_
#define CC_SERVER_HTTP_API_DISPATCH_H_

#include <httplib.h>

#include <string_view>

#include "server/server_context.h"

namespace cc::server::http {

// The name of the deprecated session cookie (Python:
// version.SESSION_COOKIE_NAME).
inline constexpr std::string_view kSessionCookie = "__ccPrivilegedAccessToken";

// Answers the API call `request` in `response`: routes it to the service
// and product of its path, checks the caller's session, and runs the
// service's handler. Errors become a TApplicationException reply. Logs one
// INFO line per call.
void HandleApiCall(ServerContext& context, const httplib::Request& request,
                   httplib::Response& response);

}  // namespace cc::server::http

#endif  // CC_SERVER_HTTP_API_DISPATCH_H_
