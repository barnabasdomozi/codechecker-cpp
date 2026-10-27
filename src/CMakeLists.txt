# The cc_* libraries and the codechecker-cpp program
# (docs/implementation/architecture.md §3–5). Project headers are included
# by their path from this folder.

add_library(cc_platform STATIC
  platform/filesystem.cc
  platform/local_time.cc
  platform/locked_file.cc
  platform/process.cc
  platform/processes.cc
  platform/signal_receiver.cc
  platform/socket_options.cc
  platform/subprocess.cc
  platform/terminal.cc)
target_include_directories(cc_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(cc_util STATIC
  util/argparse.cc
  util/base64.cc
  util/compression.cc
  util/datetime.cc
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
  util/template.cc
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
  third_party::zip third_party::zlib)

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
  report/output/baseline.cc
  report/output/codeclimate.cc
  report/output/error_file.cc
  report/output/gerrit.cc
  report/output/html.cc
  report/output/json.cc
  report/output/plaintext.cc
  report/output/plist.cc
  report/output/sarif.cc
  report/output/statistics.cc
  report/output/tool_info.cc
  report/parser/plist.cc
  report/parser/report_file.cc
  report/parser/sarif.cc)
target_link_libraries(cc_report PUBLIC cc_util)

add_library(cc_common STATIC
  common/checker_labels.cc
  common/cli/logo.cc
  common/cli/verbose.cc
  common/cli/version.cc
  common/cmd_config.cc
  common/guidelines.cc
  common/package.cc
  common/review_status_handler.cc
  common/skiplist_handler.cc
  common/source_code_comment_handler.cc
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
  analyzer/cli/check.cc
  analyzer/cli/log.cc
  analyzer/cli/parse.cc
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
  analyzer/stats/statistics.cc
  analyzer/suppress_handler.cc)
target_link_libraries(cc_analyzer PUBLIC cc_common cc_tu_collector)

if(CC_BUILD_WEB)
  # The API version header and the C++ code generated from api/*.thrift
  # (build-system.md §4.3). The generated headers are included as
  # "api/thrift/<file>.h".
  set(api_version_file ${PROJECT_SOURCE_DIR}/api/api_version.json)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               ${api_version_file})
  file(READ ${api_version_file} api_version_json)
  string(JSON CC_API_VERSION GET "${api_version_json}" api_version)
  configure_file(api/api_version.h.in
                 ${PROJECT_BINARY_DIR}/gen/api/api_version.h @ONLY)

  set(thrift_gen_dir ${PROJECT_BINARY_DIR}/gen/api/thrift)
  file(GLOB thrift_idl_files CONFIGURE_DEPENDS
       ${PROJECT_SOURCE_DIR}/api/*.thrift)
  # What the Thrift compiler writes for the IDL files.
  set(thrift_sources
    authentication_types.cpp codeCheckerAuthentication.cpp
    codeCheckerDBAccess.cpp codeCheckerProductService.cpp
    codeCheckerServersideTaskService.cpp configurationService.cpp
    products_types.cpp report_server_constants.cpp report_server_types.cpp
    serverInfoService.cpp shared_types.cpp tasks_types.cpp)
  set(thrift_headers
    authentication_types.h codeCheckerAuthentication.h codeCheckerDBAccess.h
    codeCheckerProductService.h codeCheckerServersideTaskService.h
    configurationService.h configuration_types.h products_types.h
    report_server_constants.h report_server_types.h serverInfoService.h
    server_info_types.h shared_types.h tasks_types.h)
  list(TRANSFORM thrift_sources PREPEND ${thrift_gen_dir}/)
  list(TRANSFORM thrift_headers PREPEND ${thrift_gen_dir}/)
  set(thrift_compiler ${CC_DEPS_PREFIX}/bin/thrift)
  add_custom_command(
    OUTPUT ${thrift_sources} ${thrift_headers}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${thrift_gen_dir}
    COMMAND ${CMAKE_COMMAND}
            -DTHRIFT=${thrift_compiler}
            -DIDL_DIR=${PROJECT_SOURCE_DIR}/api
            -DOUT_DIR=${thrift_gen_dir}
            -DLANGUAGE=cpp
            -P ${PROJECT_SOURCE_DIR}/cmake/ThriftGenerate.cmake
    DEPENDS ${thrift_idl_files} ${thrift_compiler}
            ${PROJECT_SOURCE_DIR}/cmake/ThriftGenerate.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/api/ordered_map.h
    COMMENT "Generating the C++ code of the Thrift API"
    VERBATIM)
  add_library(cc_api STATIC ${thrift_sources})
  # Generated code does not follow the project's warning settings.
  set_source_files_properties(${thrift_sources} PROPERTIES COMPILE_OPTIONS -w)
  # The generated code includes "api/ordered_map.h".
  target_include_directories(cc_api PUBLIC ${PROJECT_BINARY_DIR}/gen
                             ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(cc_api PUBLIC third_party::thrift)

  add_library(cc_web STATIC
    web/cli/web_version.cc
    web/convert.cc
    web/database_status.cc
    web/env.cc)
  target_link_libraries(cc_web PUBLIC cc_common cc_api)

  add_library(cc_server STATIC
    server/api/authentication_handler.cc
    server/api/configuration_handler.cc
    server/api/product_handler.cc
    server/api/report_filter.cc
    server/api/report_handler.cc
    server/api/thrift_enum_helper.cc
    server/auth/permissions.cc
    server/auth/session_manager.cc
    server/cli/server.cc
    server/database/connection.cc
    server/database/database.cc
    server/database/db_cleanup.cc
    server/database/schema.cc
    server/database/sql.cc
    server/http/api_dispatch.cc
    server/http/http_server.cc
    server/http/routing.cc
    server/package_data.cc
    server/product.cc
    server/server_config.cc
    server/supervisor/instance_manager.cc
    server/supervisor/supervisor.cc
    server/tasks/task_manager.cc)
  target_link_libraries(cc_server PUBLIC cc_web third_party::httplib
    third_party::sqlite3 third_party::pq third_party::ssl)
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
  target_link_libraries(codechecker-cpp PRIVATE cc_server)
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

# Adds the target `target`, which copies the files of the folder `source`
# (relative to the project) into the folder `destination` of the build
# folder, and installs them there.
function(cc_add_data_files target source destination)
  file(GLOB_RECURSE files CONFIGURE_DEPENDS
    RELATIVE ${PROJECT_SOURCE_DIR}/${source} ${PROJECT_SOURCE_DIR}/${source}/*)
  set(outputs)
  foreach(file IN LISTS files)
    add_custom_command(
      OUTPUT ${PROJECT_BINARY_DIR}/${destination}/${file}
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${PROJECT_SOURCE_DIR}/${source}/${file}
              ${PROJECT_BINARY_DIR}/${destination}/${file}
      DEPENDS ${PROJECT_SOURCE_DIR}/${source}/${file}
      VERBATIM)
    list(APPEND outputs ${PROJECT_BINARY_DIR}/${destination}/${file})
  endforeach()
  add_custom_target(${target} ALL DEPENDS ${outputs})
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/${source}/
          DESTINATION ${destination})
endfunction()

# The copied package data files (architecture.md §3): the config files next
# to the version files, and the HTML report templates and static files.
cc_add_data_files(config_files config config)
cc_add_data_files(html_files data/html lib/codechecker/html)

# The standalone tools (architecture.md §4), built into bin/ next to
# codechecker-cpp.
add_executable(report-converter tools/report-converter/main.cc)
target_link_libraries(report-converter PRIVATE cc_report)
set_target_properties(report-converter PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
install(TARGETS report-converter RUNTIME DESTINATION bin)
