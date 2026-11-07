// The check of the "PostgreSQL arguments" (--dbaddress, --db-host, ...)
// that `server` and `cmd products add` share: they are only allowed with
// --postgresql.

#ifndef CC_WEB_CLI_POSTGRESQL_ARGUMENTS_H_
#define CC_WEB_CLI_POSTGRESQL_ARGUMENTS_H_

#include <optional>
#include <span>
#include <string>

#include "util/argparse.h"

namespace cc::web::cli {

// Returns the parse error for the first PostgreSQL option that the command
// line arguments `args` give when `parsed`, the parse result of `args`, has
// no --postgresql: "argument <option>: not allowed without argument
// --postgresql"; nullopt if there is no such error (Python: the __handle()
// of `server` and of `cmd products add`). The options are found by prefix
// (common::ArgMatch()), because a default and a given value look the same
// in `parsed`.
std::optional<std::string> PostgresqlArgumentError(
    const util::argparse::Namespace& parsed, std::span<const std::string> args);

}  // namespace cc::web::cli

#endif  // CC_WEB_CLI_POSTGRESQL_ARGUMENTS_H_
