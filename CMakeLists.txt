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

option(CC_BUILD_WEB "Build the web side (store, cmd, server)" ON)
option(CC_BUILD_TESTS "Build the unit tests" ON)
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
install(TARGETS ldlogger RUNTIME DESTINATION bin)

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
  install(TARGETS ${target} LIBRARY DESTINATION ld_logger/lib/${bits}bit)
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

# The license of every third-party library that is linked into the programs
# (build-system.md §5). GoogleTest is only linked into the unit tests.
set(licenses
  pcre2/LICENCE.md yyjson/LICENSE libyaml/License libxml2/Copyright
  zlib/LICENSE libzip/LICENSE openssl/LICENSE.txt)
if(CC_BUILD_WEB)
  list(APPEND licenses
    curl/COPYING cpp-httplib/LICENSE thrift/LICENSE thrift/NOTICE
    boost/LICENSE_1_0.txt sqlite/LICENSE.md postgres/COPYRIGHT
    jwt-cpp/LICENSE openldap/LICENSE)
endif()
foreach(license IN LISTS licenses)
  cmake_path(GET license PARENT_PATH library)
  install(FILES third_party/${license}
          DESTINATION lib/codechecker/licenses/${library})
endforeach()

# The black-box suite on an install of this build (architecture.md §13).
set(blackbox_root ${PROJECT_BINARY_DIR}/blackbox-root)
add_custom_target(blackbox
  COMMAND ${CMAKE_COMMAND} -E rm -rf ${blackbox_root}
  COMMAND ${CMAKE_COMMAND} --install ${PROJECT_BINARY_DIR}
          --prefix ${blackbox_root}
  COMMAND ${CMAKE_COMMAND} -E env
          CODECHECKER_BIN=${blackbox_root}/bin/codechecker-cpp
          npm test
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/blackbox_tests
  USES_TERMINAL
  VERBATIM)
add_dependencies(blackbox codechecker-cpp report-converter version_files ldlogger
  ldlogger_64)
if(CC_LDLOGGER_32BIT)
  add_dependencies(blackbox ldlogger_32)
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
