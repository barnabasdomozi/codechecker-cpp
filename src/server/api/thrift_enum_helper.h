// Port of codechecker_server/api/thrift_enum_helper.py: the names under
// which the product databases keep the values of the API's enums. A value
// without a name is an internal error of the reference (an assertion): it
// fails with a GENERAL ApiError with the assertion's message.

#ifndef CC_SERVER_API_THRIFT_ENUM_HELPER_H_
#define CC_SERVER_API_THRIFT_ENUM_HELPER_H_

#include <string>
#include <string_view>

#include "api/thrift/report_server_types.h"
#include "server/api/request_failed.h"

namespace cc::server::api {

// Returns the name of `status`, for example "unresolved" (Python:
// detection_status_str()).
ApiResult<std::string> DetectionStatusName(DetectionStatus::type status);

// Returns the status named `name` (Python: detection_status_enum()).
ApiResult<DetectionStatus::type> DetectionStatusOf(std::string_view name);

// Returns the name of `status`, for example "false_positive" (Python:
// review_status_str()).
ApiResult<std::string> ReviewStatusName(ReviewStatus::type status);

// Returns the status named `name` (Python: review_status_enum()).
ApiResult<ReviewStatus::type> ReviewStatusOf(std::string_view name);

// Returns the type named `name`: "note", "macro" or "fixit" (Python:
// report_extended_data_type_enum()).
ApiResult<ExtendedReportDataType::type> ExtendedReportDataTypeOf(
    std::string_view name);

}  // namespace cc::server::api

#endif  // CC_SERVER_API_THRIFT_ENUM_HELPER_H_
