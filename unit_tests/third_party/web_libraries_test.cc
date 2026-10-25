// Checks that the web side's third-party libraries link and work with the
// settings of third_party/CMakeLists.txt.

#include <curl/curl.h>
#include <ldap.h>
#include <libpq-fe.h>
#include <sqlite3.h>

#include <memory>
#include <set>
#include <string>
#include <system_error>

#include "gtest/gtest.h"
#include "httplib.h"
#include "jwt-cpp/jwt.h"
#include "thrift/protocol/TJSONProtocol.h"
#include "thrift/transport/TBufferTransports.h"

namespace cc {
namespace {

TEST(ThirdPartyTest, CurlHasOnlyHttpWithTlsAndZlib) {
  const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
  EXPECT_NE(info->features & CURL_VERSION_SSL, 0);
  EXPECT_NE(info->features & CURL_VERSION_LIBZ, 0);
  std::set<std::string> protocols;
  for (const char* const* protocol = info->protocols; *protocol != nullptr;
       ++protocol) {
    protocols.insert(*protocol);
  }
  EXPECT_EQ(protocols, (std::set<std::string>{"http", "https"}));
}

TEST(ThirdPartyTest, HttplibCreatesTlsClient) {
  httplib::SSLClient client("localhost", 1);
  EXPECT_TRUE(client.is_valid());
}

TEST(ThirdPartyTest, ThriftJsonRoundTrip) {
  auto buffer = std::make_shared<apache::thrift::transport::TMemoryBuffer>();
  apache::thrift::protocol::TJSONProtocol protocol(buffer);
  protocol.writeString("héllo");
  std::string read;
  protocol.readString(read);
  EXPECT_EQ(read, "héllo");
}

TEST(ThirdPartyTest, SqliteRunsQueries) {
  sqlite3* db = nullptr;
  ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);
  EXPECT_EQ(sqlite3_exec(db, "CREATE TABLE t (x); INSERT INTO t VALUES (1);",
                         nullptr, nullptr, nullptr),
            SQLITE_OK);
  sqlite3_close(db);
}

TEST(ThirdPartyTest, LibpqUsesOpenssl) {
  EXPECT_EQ(PQlibVersion() / 10000, 17);
  EXPECT_STREQ(PQsslAttribute(nullptr, "library"), "OpenSSL");
}

TEST(ThirdPartyTest, JwtSignsAndVerifies) {
  const std::string token = jwt::create()
                                .set_issuer("codechecker")
                                .sign(jwt::algorithm::hs256{"secret"});
  std::error_code error;
  jwt::verify()
      .allow_algorithm(jwt::algorithm::hs256{"secret"})
      .with_issuer("codechecker")
      .verify(jwt::decode(token), error);
  EXPECT_FALSE(error) << error.message();
}

TEST(ThirdPartyTest, LibldapUsesOpenssl) {
  char* package = nullptr;
  ASSERT_EQ(ldap_get_option(nullptr, LDAP_OPT_X_TLS_PACKAGE, &package),
            LDAP_OPT_SUCCESS);
  EXPECT_STREQ(package, "OpenSSL");
  ldap_memfree(package);
}

}  // namespace
}  // namespace cc
