// Port of codechecker_client/client.py: the clients of the services of a
// server, set up with the saved session token or a login with the saved
// credentials (server-api.md §4.3, §6).

#ifndef CC_CLIENT_CLIENT_H_
#define CC_CLIENT_CLIENT_H_

#include <stdint.h>

#include <string>
#include <string_view>

#include "api/thrift/codeCheckerDBAccess.h"
#include "api/thrift/codeCheckerProductService.h"
#include "api/thrift/codeCheckerServersideTaskService.h"
#include "client/call/thrift_call.h"
#include "client/product.h"
#include "util/result.h"

namespace cc::client {

// The client of the CodeCheckerService of a product.
using ResultsClient = call::ServiceClient<codeCheckerDBAccessClient>;
// The client of the Products service.
using ProductClient = call::ServiceClient<codeCheckerProductServiceClient>;
// The client of the Tasks service.
using TaskClient = call::ServiceClient<codeCheckerServersideTaskServiceClient>;

// The functions below log what they do as the reference does, and fail with
// the errors they did not log yet in the message. A failed API call is
// already logged.

// Returns the client of the CodeCheckerService of the product at
// `product_url` (Python: setup_client()). A malformed URL fails with exit
// code 2.
Result<ResultsClient> SetupClient(const std::string& product_url);

// Returns the client of the Products service of the product of `url`, after
// checking that the product answers (Python: setup_product_client() with a
// product name).
Result<ProductClient> SetupProductClient(const ProductUrl& url);

// Returns the client of the server-wide Tasks service of the server at
// `host`:`port` (Python: setup_task_client()).
Result<TaskClient> SetupTaskClient(std::string_view protocol,
                                   std::string_view host, int64_t port);

}  // namespace cc::client

#endif  // CC_CLIENT_CLIENT_H_
