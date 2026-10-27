// What the request handlers of an API process use: the configuration, the
// configuration database, the sessions and the products (Python: the
// attributes of CCSimpleHttpServer in server.py).

#ifndef CC_SERVER_SERVER_CONTEXT_H_
#define CC_SERVER_SERVER_CONTEXT_H_

#include <string>
#include <utility>

#include "server/auth/session.h"
#include "server/auth/session_manager.h"
#include "server/database/database.h"
#include "server/package_data.h"
#include "server/product.h"
#include "server/server_config.h"

namespace cc::server {

// The state of a server process. The supervisor creates it before it forks
// the API processes, which inherit it; each process then opens its own
// database connections. Not thread-safe.
class ServerContext {
 public:
  ServerContext(ServerConfig config, db::Database config_db,
                std::string workspace, std::string package_version,
                PackageData package_data)
      : config_(std::move(config)),
        config_db_(std::move(config_db)),
        sessions_(&config_.authentication(), &config_db_),
        products_(&config_db_, workspace),
        workspace_(std::move(workspace)),
        package_version_(std::move(package_version)),
        package_data_(std::move(package_data)) {}

  ServerContext(const ServerContext&) = delete;
  ServerContext& operator=(const ServerContext&) = delete;

  const ServerConfig& config() const { return config_; }
  db::LazyConnection& config_db() { return config_db_; }
  auth::SessionManager& sessions() { return sessions_; }
  ProductRegistry& products() { return products_; }
  // The absolute path of the workspace folder.
  const std::string& workspace() const { return workspace_; }
  // The version that ServerInfo.getPackageVersion() returns: the git
  // describe tag of the build (server-api.md §2.4).
  const std::string& package_version() const { return package_version_; }
  const PackageData& package_data() const { return package_data_; }

 private:
  ServerConfig config_;
  db::LazyConnection config_db_;
  auth::SessionManager sessions_;  // Uses config_ and config_db_.
  ProductRegistry products_;       // Uses config_db_.
  std::string workspace_;
  std::string package_version_;
  PackageData package_data_;
};

// The request a handler serves: the server process, the caller's session
// (null for a guest), the product of the URL (null if none) and the API
// version of the client ("6.74").
struct Request {
  ServerContext* server;
  const auth::Session* session;
  Product* product;
  std::string api_version;
};

}  // namespace cc::server

#endif  // CC_SERVER_SERVER_CONTEXT_H_
