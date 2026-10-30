# Unit tests of cc_report (architecture.md §13).

add_executable(report_test report_hash_test.cc source_file_cache_test.cc)
target_link_libraries(report_test PRIVATE cc_report third_party::gtest)
# The shared fixture is included by its path from the project root.
target_include_directories(report_test PRIVATE ${PROJECT_SOURCE_DIR})
add_test(NAME report_test COMMAND report_test)
