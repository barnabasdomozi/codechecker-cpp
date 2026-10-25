# Builds the third-party libraries from the submodules into one prefix, which
# the project then uses (docs/implementation/build-system.md §3):
#
#   cmake -S third_party -B build/deps
#   cmake --build build/deps --parallel

cmake_minimum_required(VERSION 3.28)
project(cc_third_party LANGUAGES C CXX)

include(ExternalProject)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/DepsSources.cmake)

option(CC_DEPS_WEB "Build the libraries of the web side" ON)
option(CC_DEPS_TESTS "Build GoogleTest" ON)
set(CC_DEPS_BUILD_TYPE Release CACHE STRING
    "Build type of the libraries built with CMake")

if(NOT DEFINED CACHE{CC_OPENSSLDIR})
  # Use the build machine's OpenSSL folder, so the default CA files and
  # openssl.cnf are the ones Python's `ssl` uses.
  execute_process(COMMAND openssl version -d OUTPUT_VARIABLE out ERROR_QUIET)
  if(out MATCHES "\"(.+)\"")
    set(dir "${CMAKE_MATCH_1}")
  else()
    set(dir /usr/lib/ssl)
  endif()
  set(CC_OPENSSLDIR "${dir}" CACHE PATH "OPENSSLDIR of the built OpenSSL")
endif()

set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(prefix ${CMAKE_BINARY_DIR}/install)
set_property(DIRECTORY PROPERTY EP_BASE ${CMAKE_BINARY_DIR}/ep)

set(libraries zlib pcre2 pcre2/deps/sljit yyjson libyaml libxml2 libzip
    openssl)
if(CC_DEPS_WEB)
  list(APPEND libraries curl cpp-httplib thrift boost sqlite postgres jwt-cpp
       openldap)
endif()
if(CC_DEPS_TESTS)
  list(APPEND libraries googletest)
endif()
foreach(library IN LISTS libraries)
  if(NOT EXISTS ${src}/${library}/.git)
    message(FATAL_ERROR "third_party/${library} is not checked out. "
                        "Run third_party/checkout.sh.")
  endif()
endforeach()

# The libraries are built together: when a submodule changes, start over.
get_filename_component(root ${src} DIRECTORY)
cc_deps_sources(${root} sources)
set(sources_file ${CMAKE_BINARY_DIR}/cc-deps-sources.txt)
if(EXISTS ${sources_file})
  file(READ ${sources_file} built_from)
  if(NOT built_from STREQUAL sources)
    message(STATUS "The submodules changed: rebuilding every library")
    file(REMOVE_RECURSE ${CMAKE_BINARY_DIR}/ep ${prefix})
  endif()
endif()
file(WRITE ${sources_file} "${sources}")

if(CMAKE_GENERATOR MATCHES "Makefiles")
  set(make make)  # Uses the parent's job server (JOB_SERVER_AWARE below).
else()
  cmake_host_system_information(RESULT jobs QUERY NUMBER_OF_LOGICAL_CORES)
  set(make make -j${jobs})
endif()
set(log_args LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON
    LOG_OUTPUT_ON_FAILURE ON JOB_SERVER_AWARE ON)

# Every library finds the others only in the prefix.
set(cmake_args
    -DCMAKE_BUILD_TYPE=${CC_DEPS_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX=${prefix}
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_PREFIX_PATH=${prefix}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_DEBUG_POSTFIX=
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_TESTING=OFF
    -DZLIB_ROOT=${prefix}
    -DZLIB_USE_STATIC_LIBS=ON
    -DOPENSSL_ROOT_DIR=${prefix}
    -DOPENSSL_USE_STATIC_LIBS=ON)

# Adds a library that is built with CMake from third_party/<name>.
function(cc_cmake_library name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "DEPENDS;ARGS")
  ExternalProject_Add(${name}
    SOURCE_DIR ${src}/${name}
    CMAKE_ARGS ${cmake_args} ${arg_ARGS}
    DEPENDS ${arg_DEPENDS}
    ${log_args})
  set_property(GLOBAL APPEND PROPERTY cc_deps ${name})
endfunction()

# Adds a library that is built with configure and make from
# third_party/<name>. Libraries with dependencies get the prefix in their
# compiler and linker flags, and what a static OpenSSL needs in LIBS.
function(cc_make_library name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" ""
                        "DEPENDS;CONFIGURE;BUILD;INSTALL")
  set(env ${CMAKE_COMMAND} -E env CC=${CMAKE_C_COMPILER})
  if(arg_DEPENDS)
    list(APPEND env CPPFLAGS=-I${prefix}/include LDFLAGS=-L${prefix}/lib
         "LIBS=-pthread -ldl")
  endif()
  ExternalProject_Add(${name}
    SOURCE_DIR ${src}/${name}
    CONFIGURE_COMMAND ${env} ${arg_CONFIGURE}
    BUILD_COMMAND ${arg_BUILD}
    INSTALL_COMMAND ${arg_INSTALL}
    DEPENDS ${arg_DEPENDS}
    ${log_args})
  set_property(GLOBAL APPEND PROPERTY cc_deps ${name})
endfunction()

# Analyzer side.

# zlib's CMake renames zconf.h in the source folder; its configure does not.
cc_make_library(zlib
  CONFIGURE <SOURCE_DIR>/configure --static --prefix=${prefix}
  BUILD ${make}
  INSTALL ${make} install)

cc_cmake_library(pcre2 ARGS
  -DPCRE2_BUILD_PCRE2_8=ON
  -DPCRE2_BUILD_PCRE2_16=OFF
  -DPCRE2_BUILD_PCRE2_32=OFF
  -DPCRE2_SUPPORT_JIT=ON
  -DPCRE2_SUPPORT_UNICODE=ON
  -DPCRE2_BUILD_PCRE2GREP=OFF
  -DPCRE2_BUILD_TESTS=OFF
  -DPCRE2_SUPPORT_LIBZ=OFF
  -DPCRE2_SUPPORT_LIBBZ2=OFF
  -DPCRE2_SUPPORT_LIBEDIT=OFF
  -DPCRE2_SUPPORT_LIBREADLINE=OFF)

cc_cmake_library(yyjson ARGS
  -DYYJSON_BUILD_TESTS=OFF
  -DYYJSON_BUILD_FUZZER=OFF
  -DYYJSON_BUILD_MISC=OFF
  -DYYJSON_BUILD_DOC=OFF)

cc_cmake_library(libyaml)

cc_cmake_library(libxml2 ARGS
  -DLIBXML2_WITH_PYTHON=OFF
  -DLIBXML2_WITH_ICU=OFF
  -DLIBXML2_WITH_LZMA=OFF
  -DLIBXML2_WITH_ZLIB=OFF
  -DLIBXML2_WITH_HTTP=OFF
  -DLIBXML2_WITH_READLINE=OFF
  -DLIBXML2_WITH_PROGRAMS=OFF
  -DLIBXML2_WITH_TESTS=OFF
  -DLIBXML2_WITH_ICONV=ON)

cc_cmake_library(libzip DEPENDS zlib ARGS
  -DENABLE_BZIP2=OFF
  -DENABLE_LZMA=OFF
  -DENABLE_ZSTD=OFF
  -DENABLE_OPENSSL=OFF
  -DENABLE_GNUTLS=OFF
  -DENABLE_COMMONCRYPTO=OFF
  -DENABLE_WINDOWS_CRYPTO=OFF
  -DBUILD_TOOLS=OFF
  -DBUILD_REGRESS=OFF
  -DBUILD_OSSFUZZ=OFF
  -DBUILD_EXAMPLES=OFF
  -DBUILD_DOC=OFF)

cc_make_library(openssl
  CONFIGURE perl <SOURCE_DIR>/Configure no-shared no-apps no-docs no-tests
            --prefix=${prefix} --libdir=lib --openssldir=${CC_OPENSSLDIR}
  BUILD ${make} build_libs
  INSTALL ${make} install_dev)

# Web side.

if(CC_DEPS_WEB)
  # Only HTTP and HTTPS, over HTTP/1.1, with nothing but OpenSSL and zlib.
  set(curl_disable)
  foreach(protocol DICT FILE FTP GOPHER IMAP IPFS LDAP LDAPS MQTT POP3 RTSP
                   SMTP TELNET TFTP WEBSOCKETS)
    list(APPEND curl_disable -DCURL_DISABLE_${protocol}=ON)
  endforeach()
  cc_cmake_library(curl DEPENDS openssl zlib ARGS
    -DBUILD_STATIC_LIBS=ON
    -DBUILD_CURL_EXE=OFF
    -DBUILD_EXAMPLES=OFF
    -DBUILD_LIBCURL_DOCS=OFF
    -DBUILD_MISC_DOCS=OFF
    -DENABLE_CURL_MANUAL=OFF
    -DCURL_USE_OPENSSL=ON
    -DCURL_ZLIB=ON
    -DCURL_BROTLI=OFF
    -DCURL_ZSTD=OFF
    -DCURL_USE_LIBPSL=OFF
    -DCURL_USE_LIBSSH2=OFF
    -DCURL_USE_GSSAPI=OFF
    -DCURL_USE_PKGCONFIG=OFF
    -DUSE_LIBIDN2=OFF
    -DUSE_NGHTTP2=OFF
    -DENABLE_ARES=OFF
    -DCURL_CA_BUNDLE=auto
    -DCURL_CA_PATH=auto
    -DCURL_CA_FALLBACK=ON
    ${curl_disable})

  # Header-only libraries: copy the headers into the prefix.
  file(COPY ${src}/cpp-httplib/httplib.h DESTINATION ${prefix}/include)
  file(COPY ${src}/jwt-cpp/include/jwt-cpp ${src}/jwt-cpp/include/picojson
       DESTINATION ${prefix}/include)
  file(STRINGS ${src}/boost-libraries.txt boost_libraries REGEX "^[^#]")
  foreach(library IN LISTS boost_libraries)
    if(NOT EXISTS ${src}/boost/${library}/include)
      message(FATAL_ERROR "third_party/boost/${library} is not checked out. "
                          "Run third_party/checkout.sh.")
    endif()
    file(COPY ${src}/boost/${library}/include/ DESTINATION ${prefix}/include)
  endforeach()

  cc_cmake_library(thrift ARGS
    -DBUILD_COMPILER=ON
    -DBUILD_LIBRARIES=ON
    -DBUILD_TUTORIALS=OFF
    -DWITH_CPP=ON
    -DWITH_AS3=OFF
    -DWITH_C_GLIB=OFF
    -DWITH_JAVA=OFF
    -DWITH_JAVASCRIPT=OFF
    -DWITH_NODEJS=OFF
    -DWITH_PYTHON=OFF
    -DWITH_OPENSSL=OFF
    -DWITH_ZLIB=OFF
    -DWITH_LIBEVENT=OFF
    -DWITH_QT5=OFF
    -DBoost_INCLUDE_DIR=${prefix}/include)

  # SQLite's own build makes the amalgamation and compiles it with
  # upstream's default options.
  cc_make_library(sqlite
    CONFIGURE <SOURCE_DIR>/configure --prefix=${prefix} --disable-shared
              --disable-tcl
    BUILD ${make} lib
    INSTALL ${make} install-lib install-headers)

  # Only the static libpq. The shared one is not needed, and its build fails
  # a check (no calls to exit()) because of the static OpenSSL in it.
  set(libpq_headers <SOURCE_DIR>/src/interfaces/libpq/libpq-fe.h
                    <SOURCE_DIR>/src/interfaces/libpq/libpq-events.h)
  cc_make_library(postgres DEPENDS openssl
    CONFIGURE <SOURCE_DIR>/configure --prefix=${prefix} --with-ssl=openssl
              --without-icu --without-readline --without-zlib
    BUILD ${make} -C src/backend generated-headers
    COMMAND ${make} -C src/interfaces/libpq all-static-lib
    INSTALL ${make} -C src/include install
    COMMAND ${make} -C src/common install
    COMMAND ${make} -C src/port install
    COMMAND ${make} -C src/interfaces/libpq install-lib-static
    COMMAND ${CMAKE_COMMAND} -E copy ${libpq_headers} ${prefix}/include)

  # `make install` would also write ldap.conf to the system's /etc, so the
  # file goes into the build folder instead. The library still reads
  # /etc/openldap/ldap.conf.
  cc_make_library(openldap DEPENDS openssl
    CONFIGURE <SOURCE_DIR>/configure --prefix=${prefix} --sysconfdir=/etc
              --disable-slapd --disable-shared --enable-static
              --without-cyrus-sasl --with-tls=openssl --without-systemd
    BUILD ${make} depend
    COMMAND ${make} -C include
    COMMAND ${make} -C libraries
    INSTALL ${make} -C include install
    COMMAND ${make} -C libraries install
            sysconfdir=${CMAKE_BINARY_DIR}/ep/openldap-etc)
endif()

if(CC_DEPS_TESTS)
  cc_cmake_library(googletest ARGS
    -DBUILD_GMOCK=ON
    -DINSTALL_GTEST=ON)
endif()

# The manifest tells the project what the prefix was built from (§4.1).
file(WRITE ${CMAKE_BINARY_DIR}/cc-deps-manifest.cmake
     "set(CC_DEPS_SOURCES ${sources})\n"
     "set(CC_DEPS_WEB ${CC_DEPS_WEB})\n"
     "set(CC_DEPS_TESTS ${CC_DEPS_TESTS})\n")
get_property(deps GLOBAL PROPERTY cc_deps)
add_custom_target(manifest ALL
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/cc-deps-manifest.cmake
          ${prefix}/)
add_dependencies(manifest ${deps})
