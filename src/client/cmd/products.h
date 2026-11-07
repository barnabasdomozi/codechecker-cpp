// Port of codechecker_client/product_client.py: the actions of
// `cmd products`, which list, add and remove the products of a server
// (cmd-products.md).

#ifndef CC_CLIENT_CMD_PRODUCTS_H_
#define CC_CLIENT_CMD_PRODUCTS_H_

#include "util/argparse.h"
#include "util/result.h"

namespace cc::client::cmd {

// These functions run an action of `cmd products` with the options in
// `args`. Errors that the result does not carry a message for are already
// logged.

// Runs `cmd products list` (Python: handle_list_products()).
Result<void> ListProducts(const util::argparse::Namespace& args);

// Runs `cmd products add` (Python: handle_add_product()). `args` must have
// the defaults that depend on the endpoint already filled in: "sqlite",
// "dbusername" and "dbname" (see the add action in client/cli/cmd.cc).
Result<void> AddProduct(const util::argparse::Namespace& args);

// Runs `cmd products del` (Python: handle_del_product()). A product that
// does not exist is logged as an error but is not a failure
// (known-issues #39).
Result<void> DelProduct(const util::argparse::Namespace& args);

}  // namespace cc::client::cmd

#endif  // CC_CLIENT_CMD_PRODUCTS_H_
