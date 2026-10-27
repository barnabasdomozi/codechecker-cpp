#include "server/api/thrift_enum_helper.h"

#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "api/thrift/report_server_types.h"
#include "api/thrift/shared_types.h"
#include "server/api/request_failed.h"

namespace cc::server::api {
namespace {

// The names of the detection statuses.
constexpr std::pair<DetectionStatus::type, std::string_view>
    kDetectionStatuses[] = {{DetectionStatus::NEW, "new"},
                            {DetectionStatus::RESOLVED, "resolved"},
                            {DetectionStatus::UNRESOLVED, "unresolved"},
                            {DetectionStatus::REOPENED, "reopened"},
                            {DetectionStatus::OFF, "off"},
                            {DetectionStatus::UNAVAILABLE, "unavailable"}};

// The names of the review statuses.
constexpr std::pair<ReviewStatus::type, std::string_view> kReviewStatuses[] =
    {{ReviewStatus::UNREVIEWED, "unreviewed"},
     {ReviewStatus::CONFIRMED, "confirmed"},
     {ReviewStatus::FALSE_POSITIVE, "false_positive"},
     {ReviewStatus::INTENTIONAL, "intentional"}};

// The names of the extended report data types.
constexpr std::pair<ExtendedReportDataType::type, std::string_view>
    kExtendedDataTypes[] = {{ExtendedReportDataType::NOTE, "note"},
                            {ExtendedReportDataType::MACRO, "macro"},
                            {ExtendedReportDataType::FIXIT, "fixit"}};

// Returns the error that a failed assertion of the reference becomes.
ApiError AssertionFailed(std::string message) {
  return ApiError{.code = ErrorCode::GENERAL, .message = std::move(message)};
}

}  // namespace

ApiResult<std::string> DetectionStatusName(DetectionStatus::type status) {
  using Name = ApiResult<std::string>;
  for (const auto& [value, name] : kDetectionStatuses) {
    if (value == status) return Name(std::string(name));
  }
  // Python's message names the review status here.
  return Name(AssertionFailed(
      std::format("Unknown review status: {}", static_cast<int>(status))));
}

ApiResult<DetectionStatus::type> DetectionStatusOf(std::string_view name) {
  using Status = ApiResult<DetectionStatus::type>;
  for (const auto& [value, status_name] : kDetectionStatuses) {
    if (status_name == name) return Status(value);
  }
  return Status(
      AssertionFailed(std::format("Unknown detection status: {}", name)));
}

ApiResult<std::string> ReviewStatusName(ReviewStatus::type status) {
  using Name = ApiResult<std::string>;
  for (const auto& [value, name] : kReviewStatuses) {
    if (value == status) return Name(std::string(name));
  }
  return Name(AssertionFailed(
      std::format("Unknown review status: {}", static_cast<int>(status))));
}

ApiResult<ReviewStatus::type> ReviewStatusOf(std::string_view name) {
  using Status = ApiResult<ReviewStatus::type>;
  for (const auto& [value, status_name] : kReviewStatuses) {
    if (status_name == name) return Status(value);
  }
  return Status(
      AssertionFailed(std::format("Unknown review status: {}", name)));
}

ApiResult<ExtendedReportDataType::type> ExtendedReportDataTypeOf(
    std::string_view name) {
  using Type = ApiResult<ExtendedReportDataType::type>;
  for (const auto& [value, type_name] : kExtendedDataTypes) {
    if (type_name == name) return Type(value);
  }
  return Type(AssertionFailed(
      std::format("Unknown ExtendedReportDataType: {}", name)));
}

}  // namespace cc::server::api
