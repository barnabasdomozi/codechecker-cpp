# The CodeChecker C++ port (docs/implementation/build-system.md §4). Build
# the third-party libraries first (§3), then the project:
#
#   cmake --preset deps && cmake --build --preset deps --parallel
#   cmake --preset dev && cmake --build --preset dev && ctest --preset dev

cmake_minimum_required(VERSION 3.28)
# The version the port reports is the reference's (architecture.md, open
# question 1). An empty CC_VERSION_RC means a release, not a release
# candidate.
project(CodeChecker VERSION 6.30.0 LANGUAGES C CXX)
set(CC_VERSION_RC "")

# The deps preset builds only the third-party libraries (§3), into
# build/deps/install.
option(CC_DEPS_ONLY "Build only the third-party libraries" OFF)
if(CC_DEPS_ONLY)
  add_subdirectory(third_party)
  return()
endif()

# The build folder is the package (architecture.md §4): every file a program
# needs at run time is built or copied into it, and there is no install step.
set(CMAKE_SKIP_INSTALL_RULES ON)

option(CC_BUILD_WEB "Build the web side (store, cmd, server)" ON)
option(CC_BUILD_TESTS "Build the unit tests" ON)
option(CC_BUILD_WEB_UI "Build the web UI (www/) with npm" ${CC_BUILD_WEB})
set(CC_WEB_UI_DIR "" CACHE PATH
    "A prebuilt www/ folder to copy instead of building the web UI")
option(CC_LDLOGGER_32BIT
       "Also build the 32-bit ldlogger.so (needs gcc-multilib)" ON)
option(CC_WERROR "Treat warnings in project code as errors" OFF)
set(CC_SANITIZE "" CACHE STRING
    "Sanitizers for project code, for example address;undefined")
set(CC_DEPS_PREFIX ${PROJECT_SOURCE_DIR}/build/deps/install CACHE PATH
    "Where the third-party libraries were installed")

include(cmake/ThirdParty.cmake)

# Settings for project code. They apply only to C++: the only C code is
# ldlogger, which keeps the flags of its Makefile.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>")
if(CC_WERROR)
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Werror>")
endif()
if(CC_SANITIZE)
  list(JOIN CC_SANITIZE "," sanitizers)
  add_compile_options(
    "$<$<COMPILE_LANGUAGE:CXX>:-fsanitize=${sanitizers};-fno-omit-frame-pointer>")
  add_link_options("$<$<LINK_LANGUAGE:CXX>:-fsanitize=${sanitizers}>")
endif()
# Stack traces name functions (libraries.md, "Errors and stack traces").
add_link_options("$<$<LINK_LANGUAGE:CXX>:-rdynamic>")

# ldlogger (build-system.md §4.4): the copied C sources, built with the
# flags of the Python project's build-logger Makefile and nothing else. No
# build-type flags: the Makefile has none, and ldlogger uses assert().
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_C_FLAGS_${config} "")
endforeach()
set(ldlogger_sources
    ldlogger/ldlogger-logger.c
    ldlogger/ldlogger-tool.c
    ldlogger/ldlogger-tool-gcc.c
    ldlogger/ldlogger-tool-javac.c
    ldlogger/ldlogger-util.c)
set(ldlogger_flags -std=c99 -pedantic -Wall -Wextra -O2)

add_executable(ldlogger ${ldlogger_sources})
target_compile_definitions(ldlogger PRIVATE _GNU_SOURCE __LOGGER_MAIN__)
target_compile_options(ldlogger PRIVATE ${ldlogger_flags})
set_target_properties(ldlogger PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# Adds ldlogger.so for one word size, 64 or 32.
function(cc_add_ldlogger_library bits)
  set(target ldlogger_${bits})
  add_library(${target} SHARED ldlogger/ldlogger-hooks.c ${ldlogger_sources})
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME ldlogger
    PREFIX ""
    NO_SONAME ON
    LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/ld_logger/lib/${bits}bit)
  target_compile_definitions(${target} PRIVATE _GNU_SOURCE)
  target_compile_options(${target} PRIVATE ${ldlogger_flags}
    -Wno-strict-aliasing -fno-exceptions -fomit-frame-pointer
    -fvisibility=hidden -m${bits})
  target_link_options(${target} PRIVATE -m${bits} -Wl,--no-as-needed)
  target_link_libraries(${target} PRIVATE dl)
endfunction()

cc_add_ldlogger_library(64)
if(CC_LDLOGGER_32BIT)
  include(CheckCSourceCompiles)
  set(CMAKE_REQUIRED_FLAGS -m32)
  set(CMAKE_REQUIRED_LINK_OPTIONS -m32)
  check_c_source_compiles("#include <stdio.h>\nint main(void) { return 0; }"
                          CC_HAVE_M32)
  unset(CMAKE_REQUIRED_FLAGS)
  unset(CMAKE_REQUIRED_LINK_OPTIONS)
  if(NOT CC_HAVE_M32)
    message(FATAL_ERROR "The 32-bit ldlogger.so needs gcc-multilib. "
                        "Install it, or set CC_LDLOGGER_32BIT=OFF.")
  endif()
  cc_add_ldlogger_library(32)
endif()

add_subdirectory(src)

# The web UI (build-system.md §4.5): the copied sources are built with npm in
# the build folder, with the JavaScript stubs of our Thrift compiler, into
# www/ next to bin/. A prebuilt folder given with CC_WEB_UI_DIR is copied
# there instead.
set(www_dir ${PROJECT_BINARY_DIR}/www)
if(CC_BUILD_WEB AND CC_BUILD_WEB_UI)
  set(web_ui_root ${PROJECT_BINARY_DIR}/web-ui)
  set(vue_dir ${web_ui_root}/web/server/vue-cli)
  set(api_js_dir ${web_ui_root}/codechecker_api/js)
  file(GLOB_RECURSE web_ui_sources CONFIGURE_DEPENDS
       ${PROJECT_SOURCE_DIR}/web-ui/*)
  file(GLOB thrift_idl_files ${PROJECT_SOURCE_DIR}/api/*.thrift)
  find_program(CC_NPM npm REQUIRED)
  file(MAKE_DIRECTORY ${vue_dir})
  add_custom_command(
    OUTPUT ${vue_dir}/node_modules/.cc-npm-ci
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_SOURCE_DIR}/web-ui/package.json
            ${PROJECT_SOURCE_DIR}/web-ui/package-lock.json
            ${PROJECT_SOURCE_DIR}/web-ui/.npmrc ${vue_dir}
    COMMAND ${CC_NPM} ci --no-audit --no-fund
    COMMAND ${CMAKE_COMMAND} -E touch ${vue_dir}/node_modules/.cc-npm-ci
    DEPENDS ${PROJECT_SOURCE_DIR}/web-ui/package-lock.json
    WORKING_DIRECTORY ${vue_dir}
    COMMENT "Installing the npm packages of the web UI"
    VERBATIM)
  add_custom_command(
    OUTPUT ${www_dir}/index.html
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/web-ui ${vue_dir}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${api_js_dir}
    COMMAND ${CMAKE_COMMAND}
            -DTHRIFT=${CC_DEPS_PREFIX}/bin/thrift
            -DIDL_DIR=${PROJECT_SOURCE_DIR}/api
            -DOUT_DIR=${api_js_dir}
            -DLANGUAGE=js
            -P ${PROJECT_SOURCE_DIR}/cmake/ThriftGenerate.cmake
    COMMAND ${CMAKE_COMMAND} -E copy
            ${PROJECT_SOURCE_DIR}/api/api_version.json
            ${web_ui_root}/codechecker_api/api_version.json
    COMMAND ${CC_NPM} run build
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${www_dir}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${vue_dir}/dist ${www_dir}
    DEPENDS ${web_ui_sources} ${thrift_idl_files}
            ${vue_dir}/node_modules/.cc-npm-ci
            ${PROJECT_SOURCE_DIR}/cmake/ThriftGenerate.cmake
    WORKING_DIRECTORY ${vue_dir}
    COMMENT "Building the web UI"
    VERBATIM)
  add_custom_target(web_ui ALL DEPENDS ${www_dir}/index.html)
elseif(CC_BUILD_WEB AND CC_WEB_UI_DIR)
  file(GLOB_RECURSE web_ui_files CONFIGURE_DEPENDS ${CC_WEB_UI_DIR}/*)
  add_custom_command(
    OUTPUT ${PROJECT_BINARY_DIR}/web-ui/.cc-copied
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${www_dir}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CC_WEB_UI_DIR} ${www_dir}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/web-ui
    COMMAND ${CMAKE_COMMAND} -E touch ${PROJECT_BINARY_DIR}/web-ui/.cc-copied
    DEPENDS ${web_ui_files}
    COMMENT "Copying the prebuilt web UI"
    VERBATIM)
  add_custom_target(web_ui ALL DEPENDS ${PROJECT_BINARY_DIR}/web-ui/.cc-copied)
endif()

# The license of every third-party library that is linked into the programs
# (build-system.md §5), copied into lib/codechecker/licenses/<library>/.
# GoogleTest is only linked into the unit tests.
set(licenses
  pcre2/LICENCE.md yyjson/LICENSE libyaml/License libxml2/Copyright
  zlib/LICENSE libzip/LICENSE openssl/LICENSE.txt)
if(CC_BUILD_WEB)
  list(APPEND licenses
    curl/COPYING cpp-httplib/LICENSE thrift/LICENSE thrift/NOTICE
    boost/LICENSE_1_0.txt sqlite/LICENSE.md postgres/COPYRIGHT
    jwt-cpp/LICENSE openldap/LICENSE)
endif()
set(license_outputs)
foreach(license IN LISTS licenses)
  cmake_path(GET license PARENT_PATH library)
  cmake_path(GET license FILENAME name)
  set(output ${PROJECT_BINARY_DIR}/lib/codechecker/licenses/${library}/${name})
  add_custom_command(
    OUTPUT ${output}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_SOURCE_DIR}/third_party/${license} ${output}
    DEPENDS ${PROJECT_SOURCE_DIR}/third_party/${license}
    VERBATIM)
  list(APPEND license_outputs ${output})
endforeach()
add_custom_target(licenses ALL DEPENDS ${license_outputs})

# The black-box suite on the build folder (architecture.md §13).
add_custom_target(blackbox
  COMMAND ${CMAKE_COMMAND} -E env
          CODECHECKER_BIN=${PROJECT_BINARY_DIR}/bin/codechecker-cpp
          npm test
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/blackbox_tests
  USES_TERMINAL
  VERBATIM)
add_dependencies(blackbox codechecker-cpp report-converter version_files
  config_files html_files ldlogger ldlogger_64)
if(CC_LDLOGGER_32BIT)
  add_dependencies(blackbox ldlogger_32)
endif()
if(TARGET web_ui)
  add_dependencies(blackbox web_ui)
endif()

if(CC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(unit_tests/third_party)
  add_subdirectory(unit_tests/util)
  add_subdirectory(unit_tests/report)
  # Programs need no shared library but glibc, libstdc++ and libgcc_s.
  string(CONCAT programs
    "$<TARGET_FILE:ldlogger>;$<TARGET_FILE:codechecker-cpp>;"
    "$<TARGET_FILE:report-converter>;"
    "$<TARGET_FILE:third_party_test>")
  add_test(NAME run_time_dependencies
    COMMAND ${CMAKE_COMMAND} "-DFILES=${programs}"
            -P ${PROJECT_SOURCE_DIR}/cmake/CheckNeeded.cmake)
endif()
