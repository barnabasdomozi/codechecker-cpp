# Checks the imported targets of cmake/ThirdParty.cmake.

add_executable(third_party_test analyzer_libraries_test.cc)
target_link_libraries(third_party_test PRIVATE
  third_party::zlib
  third_party::pcre2
  third_party::yyjson
  third_party::yaml
  third_party::xml2
  third_party::zip
  third_party::ssl
  third_party::gtest)
if(CC_BUILD_WEB)
  target_sources(third_party_test PRIVATE web_libraries_test.cc)
  target_link_libraries(third_party_test PRIVATE
    third_party::curl
    third_party::httplib
    third_party::thrift
    third_party::sqlite3
    third_party::pq
    third_party::jwt
    third_party::ldap)
endif()
add_test(NAME third_party_test COMMAND third_party_test)
