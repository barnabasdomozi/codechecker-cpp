#include "web/cli/postgresql_arguments.h"

#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/util.h"
#include "util/argparse.h"

namespace cc::web::cli {
namespace {

// The options looked for, in the order of the reference. --dbpassword and
// --db-password are not among them.
constexpr std::string_view kPostgresqlOptions[] = {
    "--dbaddress", "--dbport",  "--dbusername", "--dbname",
    "--db-host",   "--db-port", "--db-username", "--db-name"};

}  // namespace

std::optional<std::string> PostgresqlArgumentError(
    const util::argparse::Namespace& parsed,
    std::span<const std::string> args) {
  if (parsed.Has("postgresql")) return std::nullopt;
  const std::vector<std::string> given =
      common::ArgMatch(kPostgresqlOptions, args);
  if (given.empty()) return std::nullopt;
  return std::format("argument {}: not allowed without argument --postgresql",
                     given.front());
}

}  // namespace cc::web::cli
