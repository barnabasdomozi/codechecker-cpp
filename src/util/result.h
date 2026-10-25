// The result type of functions that can fail (architecture.md §5.4).

#ifndef CC_UTIL_RESULT_H_
#define CC_UTIL_RESULT_H_

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace cc {

// Why an operation failed and how the command ends because of it.
struct Error {
  // What to log. Empty if the failure was already reported.
  std::string message = {};
  // The exit code of the command. 0 means "stop here without failing",
  // for example after printing a help text.
  int exit_code = 1;
};

// Either the value of a successful operation or an Error. Callers pass
// errors up to the subcommand's run function, which reports them and
// returns the exit code.
//
//   Result<std::string> text = ReadFile(path);
//   if (!text.ok()) return Result<Config>(text.error());
//   Use(text.value());
template <typename T>
class [[nodiscard]] Result {
 public:
  explicit Result(T value) : content_(std::move(value)) {}
  explicit Result(Error error) : content_(std::move(error)) {}

  Result(const Result& other) = default;
  Result& operator=(const Result& other) = default;
  Result(Result&& other) = default;
  Result& operator=(Result&& other) = default;

  bool ok() const { return content_.index() == 0; }

  // The value; only valid if ok().
  const T& value() const& { return std::get<0>(content_); }
  T&& value() && { return std::get<0>(std::move(content_)); }

  // The error; only valid if !ok().
  const Error& error() const { return std::get<1>(content_); }

 private:
  std::variant<T, Error> content_;
};

// The result of an operation that has no value: success, or an Error.
template <>
class [[nodiscard]] Result<void> {
 public:
  Result() = default;
  explicit Result(Error error) : error_(std::move(error)) {}

  Result(const Result& other) = default;
  Result& operator=(const Result& other) = default;
  Result(Result&& other) = default;
  Result& operator=(Result&& other) = default;

  bool ok() const { return !error_.has_value(); }

  // The error; only valid if !ok().
  const Error& error() const { return *error_; }

 private:
  std::optional<Error> error_;
};

}  // namespace cc

#endif  // CC_UTIL_RESULT_H_
