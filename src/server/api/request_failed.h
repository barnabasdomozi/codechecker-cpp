// The errors of the API handlers. Handlers report errors by throwing the
// Thrift exception RequestFailed, as the Thrift interface requires; this is
// one of the places where the project uses exceptions (cppguide.md W1).
// Also the conversions of the handlers that fail with such an error.

#ifndef CC_SERVER_API_REQUEST_FAILED_H_
#define CC_SERVER_API_REQUEST_FAILED_H_

#include <optional>
#include <string>
#include <utility>

#include "api/thrift/shared_types.h"
#include "util/result.h"
#include "web/convert.h"

namespace cc::server::api {

// Returns a RequestFailed with `code` and `message` (Python:
// RequestFailed(code, message)).
inline ::RequestFailed MakeRequestFailed(ErrorCode::type code,
                                         const std::string& message) {
  ::RequestFailed failed;
  failed.__set_errorCode(code);
  failed.__set_message(message);
  return failed;
}

// Returns the value of `result`; throws RequestFailed with `code` and the
// error's message if it failed.
template <typename T>
T ValueOrThrow(Result<T> result, ErrorCode::type code = ErrorCode::DATABASE) {
  if (!result.ok()) throw MakeRequestFailed(code, result.error().message);
  return std::move(result).value();
}

// Throws RequestFailed with `code` and the error's message if `result`
// failed.
inline void ThrowIfFailed(const Result<void>& result,
                          ErrorCode::type code = ErrorCode::DATABASE) {
  if (!result.ok()) throw MakeRequestFailed(code, result.error().message);
}

// Returns the text of the Base64 `text` (web::FromBase64()); throws GENERAL
// "Incorrect padding", Python's binascii.Error, if it is not Base64.
inline std::string DecodeBase64(const std::string& text) {
  std::optional<std::string> decoded = web::FromBase64(text);
  if (!decoded) {
    throw MakeRequestFailed(ErrorCode::GENERAL, "Incorrect padding");
  }
  return *std::move(decoded);
}

}  // namespace cc::server::api

#endif  // CC_SERVER_API_REQUEST_FAILED_H_
