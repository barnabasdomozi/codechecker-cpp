# The cc_* libraries and the codechecker-cpp program
# (docs/implementation/architecture.md §3–5). Project headers are included
# by their path from this folder.

add_library(cc_platform STATIC
  platform/filesystem.cc
  platform/local_time.cc
  platform/process.cc
  platform/terminal.cc)
target_include_directories(cc_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(cc_util STATIC
  util/argparse.cc
  util/file.cc
  util/hash.cc
  util/json.cc
  util/logging.cc
  util/plist.cc
  util/print.cc
  util/pypath.cc
  util/pyre.cc
  util/strings.cc
  util/terminal.cc
  util/textwrap.cc
  util/twodim.cc
  util/unicode.cc
  util/xml.cc
  util/yaml.cc)
target_link_libraries(cc_util PUBLIC cc_platform third_party::pcre2
  third_party::yyjson third_party::yaml third_party::xml2 third_party::crypto)

add_library(cc_report STATIC
  report/converter/analyzer_types.cc
  report/converter/clang_tidy.cc
  report/converter/json_formats.cc
  report/converter/line_formats.cc
  report/converter/line_parser.cc
  report/converter/result_file_formats.cc
  report/converter/sanitizers.cc
  report/converter/xml_formats.cc
  report/hash/report_hash.cc
  report/model/report.cc
  report/output/plist.cc
  report/output/sarif.cc
  report/output/tool_info.cc
  report/parser/plist.cc
  report/parser/sarif.cc)
target_link_libraries(cc_report PUBLIC cc_util)

add_library(cc_common STATIC
  common/cli/version.cc
  common/package.cc
  common/util.cc
  common/version.cc)
target_link_libraries(cc_common PUBLIC cc_report)

add_library(cc_analyzer STATIC
  analyzer/cli/analyzer_version.cc)
target_link_libraries(cc_analyzer PUBLIC cc_common)

if(CC_BUILD_WEB)
  # The API version header (build-system.md §4.3). The Thrift code generated
  # from api/*.thrift joins it when the client and the server are ported.
  set(api_version_file ${PROJECT_SOURCE_DIR}/api/api_version.json)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               ${api_version_file})
  file(READ ${api_version_file} api_version_json)
  string(JSON CC_API_VERSION GET "${api_version_json}" api_version)
  configure_file(api/api_version.h.in
                 ${PROJECT_BINARY_DIR}/gen/api/api_version.h @ONLY)
  add_library(cc_api INTERFACE)
  target_include_directories(cc_api INTERFACE ${PROJECT_BINARY_DIR}/gen)

  add_library(cc_web STATIC
    web/cli/web_version.cc)
  target_link_libraries(cc_web PUBLIC cc_common cc_api)
endif()

# The program is built into bin/ and the version files into config/, so the
# build folder has the layout of the install tree and the program runs from
# it.
add_executable(codechecker-cpp
  launcher/main.cc
  launcher/subcommands.cc)
target_link_libraries(codechecker-cpp PRIVATE cc_analyzer)
if(CC_BUILD_WEB)
  target_sources(codechecker-cpp PRIVATE launcher/web_subcommands.cc)
  target_link_libraries(codechecker-cpp PRIVATE cc_web)
else()
  target_sources(codechecker-cpp PRIVATE launcher/no_web_subcommands.cc)
endif()
set_target_properties(codechecker-cpp PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
install(TARGETS codechecker-cpp RUNTIME DESTINATION bin)

set(version_files ${PROJECT_BINARY_DIR}/config/analyzer_version.json)
if(CC_BUILD_WEB)
  list(APPEND version_files ${PROJECT_BINARY_DIR}/config/web_version.json)
endif()
add_custom_target(version_files ALL
  COMMAND ${CMAKE_COMMAND}
          -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
          "-DOUTPUTS=${version_files}"
          -DVERSION_MAJOR=${PROJECT_VERSION_MAJOR}
          -DVERSION_MINOR=${PROJECT_VERSION_MINOR}
          -DVERSION_REVISION=${PROJECT_VERSION_PATCH}
          -DVERSION_RC=${CC_VERSION_RC}
          -P ${PROJECT_SOURCE_DIR}/cmake/VersionFiles.cmake
  BYPRODUCTS ${version_files}
  COMMENT "Writing the version files"
  VERBATIM)
install(FILES ${version_files} DESTINATION config)

# The standalone tools (architecture.md §4), built into bin/ next to
# codechecker-cpp.
add_executable(report-converter tools/report-converter/main.cc)
target_link_libraries(report-converter PRIVATE cc_report)
set_target_properties(report-converter PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
install(TARGETS report-converter RUNTIME DESTINATION bin)
