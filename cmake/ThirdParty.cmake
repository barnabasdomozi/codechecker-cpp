# Imported targets for the third-party libraries in ${CC_DEPS_PREFIX}
# (docs/implementation/build-system.md §4.1). Nothing is searched for: every
# target names exact files in the prefix, so a copy of a library installed
# on the build machine is never used.

include(${CMAKE_CURRENT_LIST_DIR}/DepsSources.cmake)

set(manifest ${CC_DEPS_PREFIX}/cc-deps-manifest.cmake)
set(CC_DEPS_SOURCES "")
if(EXISTS ${manifest})
  include(${manifest})
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${manifest})
endif()
cc_deps_sources(${PROJECT_SOURCE_DIR} sources)
if(NOT CC_DEPS_SOURCES STREQUAL sources)
  message(FATAL_ERROR
    "The third-party libraries in ${CC_DEPS_PREFIX} are missing or were "
    "built from other sources. Build them with:\n"
    "  third_party/checkout.sh\n"
    "  cmake --preset deps\n"
    "  cmake --build --preset deps --parallel")
endif()
if(CC_BUILD_WEB AND NOT CC_DEPS_WEB)
  message(FATAL_ERROR "CC_BUILD_WEB=ON needs the third-party libraries "
                      "built with -DCC_DEPS_WEB=ON.")
endif()
if(CC_BUILD_TESTS AND NOT CC_DEPS_TESTS)
  message(FATAL_ERROR "CC_BUILD_TESTS=ON needs the third-party libraries "
                      "built with -DCC_DEPS_TESTS=ON.")
endif()

find_package(Threads REQUIRED)

# Defines third_party::<name> from archives in ${CC_DEPS_PREFIX}/lib. Its
# headers are in ${CC_DEPS_PREFIX}/include, or in the INCLUDE subfolder.
function(cc_third_party name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "INCLUDE" "ARCHIVES;DEFINES;LINKS")
  add_library(third_party::${name} INTERFACE IMPORTED)
  list(TRANSFORM arg_ARCHIVES PREPEND ${CC_DEPS_PREFIX}/lib/
       OUTPUT_VARIABLE archives)
  target_include_directories(third_party::${name} INTERFACE
                             ${CC_DEPS_PREFIX}/include/${arg_INCLUDE})
  target_compile_definitions(third_party::${name} INTERFACE ${arg_DEFINES})
  target_link_libraries(third_party::${name} INTERFACE
                        ${archives} ${arg_LINKS})
  set_property(GLOBAL APPEND PROPERTY cc_third_party_archives
               ${arg_ARCHIVES})
endfunction()

cc_third_party(zlib ARCHIVES libz.a)
cc_third_party(pcre2 ARCHIVES libpcre2-8.a
               DEFINES PCRE2_CODE_UNIT_WIDTH=8 PCRE2_STATIC)
cc_third_party(yyjson ARCHIVES libyyjson.a)
cc_third_party(yaml ARCHIVES libyaml.a DEFINES YAML_DECLARE_STATIC)
cc_third_party(xml2 ARCHIVES libxml2.a INCLUDE libxml2
               DEFINES LIBXML_STATIC LINKS m)
cc_third_party(zip ARCHIVES libzip.a LINKS third_party::zlib)
cc_third_party(crypto ARCHIVES libcrypto.a
               LINKS Threads::Threads ${CMAKE_DL_LIBS})
cc_third_party(ssl ARCHIVES libssl.a LINKS third_party::crypto)

if(CC_BUILD_WEB)
  cc_third_party(curl ARCHIVES libcurl.a DEFINES CURL_STATICLIB
                 LINKS third_party::ssl third_party::zlib)
  # The server listens with the backlog of the Python server (server.md §5).
  cc_third_party(httplib
                 DEFINES CPPHTTPLIB_OPENSSL_SUPPORT CPPHTTPLIB_LISTEN_BACKLOG=511
                 LINKS third_party::ssl)
  cc_third_party(thrift ARCHIVES libthrift.a LINKS Threads::Threads)
  cc_third_party(sqlite3 ARCHIVES libsqlite3.a
                 LINKS Threads::Threads m ${CMAKE_DL_LIBS})
  cc_third_party(pq ARCHIVES libpq.a libpgcommon_shlib.a libpgport_shlib.a
                 LINKS third_party::ssl m)
  cc_third_party(jwt LINKS third_party::crypto)
  cc_third_party(ldap ARCHIVES libldap.a liblber.a LINKS third_party::ssl)
endif()

if(CC_BUILD_TESTS)
  cc_third_party(gtest ARCHIVES libgtest_main.a libgmock.a libgtest.a
                 LINKS Threads::Threads)
endif()

# Keep the statically linked third-party symbols out of the programs'
# dynamic symbol tables (build-system.md §4.2).
get_property(archives GLOBAL PROPERTY cc_third_party_archives)
list(JOIN archives ":" archives)
add_link_options("$<$<LINK_LANGUAGE:CXX>:-Wl,--exclude-libs,${archives}>")
