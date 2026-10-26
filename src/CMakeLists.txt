# The cc_* libraries and the codechecker-cpp program
# (docs/implementation/architecture.md §3–5). Project headers are included
# by their path from this folder.

add_library(cc_platform STATIC
  platform/filesystem.cc
  platform/local_time.cc
  platform/process.cc
  platform/subprocess.cc
  platform/terminal.cc)
target_include_directories(cc_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(cc_util STATIC
  util/argparse.cc
  util/file.cc
  util/fnmatch.cc
  util/hash.cc
  util/json.cc
  util/logging.cc
  util/parallel.cc
  util/plist.cc
  util/print.cc
  util/pypath.cc
  util/pyre.cc
  util/semver.cc
  util/shlex.cc
  util/strings.cc
  util/terminal.cc
  util/textwrap.cc
  util/twodim.cc
  util/unicode.cc
  util/uuid.cc
  util/xml.cc
  util/yaml.cc
  util/zip.cc)
target_link_libraries(cc_util PUBLIC cc_platform third_party::pcre2
  third_party::yyjson third_party::yaml third_party::xml2 third_party::crypto
  third_party::zip)

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
  report/output/error_file.cc
  report/output/plist.cc
  report/output/sarif.cc
  report/output/tool_info.cc
  report/parser/plist.cc
  report/parser/sarif.cc)
target_link_libraries(cc_report PUBLIC cc_util)

add_library(cc_common STATIC
  common/checker_labels.cc
  common/cli/verbose.cc
  common/cli/version.cc
  common/cmd_config.cc
  common/package.cc
  common/review_status_handler.cc
  common/skiplist_handler.cc
  common/util.cc
  common/version.cc)
target_link_libraries(cc_common PUBLIC cc_report)

add_library(cc_tu_collector STATIC
  tu_collector/tu_collector.cc)
target_link_libraries(cc_tu_collector PUBLIC cc_common)

add_library(cc_analyzer STATIC
  analyzer/analysis/analysis_manager.cc
  analyzer/analysis/makefile.cc
  analyzer/analysis/pre_analysis_manager.cc
  analyzer/buildlog/compilation_database.cc
  analyzer/buildlog/log_parser.cc
  analyzer/cli/analyze.cc
  analyzer/cli/analyzer_version.cc
  analyzer/context/analyzer_context.cc
  analyzer/context/run_command.cc
  analyzer/ctu/ctu_manager.cc
  analyzer/ctu/ctu_triple_arch.cc
  analyzer/drivers/analyzer.cc
  analyzer/drivers/analyzer_types.cc
  analyzer/drivers/clangsa.cc
  analyzer/drivers/clangtidy.cc
  analyzer/drivers/cppcheck.cc
  analyzer/drivers/gcc.cc
  analyzer/drivers/infer.cc
  analyzer/drivers/result_handler.cc
  analyzer/host_check.cc
  analyzer/metadata/metadata.cc
  analyzer/stats/statistics.cc)
target_link_libraries(cc_analyzer PUBLIC cc_common cc_tu_collector)

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

# The copied package data files (architecture.md §3), next to the version
# files.
file(GLOB_RECURSE config_files CONFIGURE_DEPENDS
  RELATIVE ${PROJECT_SOURCE_DIR}/config ${PROJECT_SOURCE_DIR}/config/*)
set(config_outputs)
foreach(file IN LISTS config_files)
  add_custom_command(
    OUTPUT ${PROJECT_BINARY_DIR}/config/${file}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_SOURCE_DIR}/config/${file}
            ${PROJECT_BINARY_DIR}/config/${file}
    DEPENDS ${PROJECT_SOURCE_DIR}/config/${file}
    VERBATIM)
  list(APPEND config_outputs ${PROJECT_BINARY_DIR}/config/${file})
endforeach()
add_custom_target(config_files ALL DEPENDS ${config_outputs})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/config/ DESTINATION config)

# The standalone tools (architecture.md §4), built into bin/ next to
# codechecker-cpp.
add_executable(report-converter tools/report-converter/main.cc)
target_link_libraries(report-converter PRIVATE cc_report)
set_target_properties(report-converter PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
install(TARGETS report-converter RUNTIME DESTINATION bin)
