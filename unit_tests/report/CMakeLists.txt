# Unit tests of cc_report (architecture.md §13).

add_executable(report_test report_hash_test.cc source_file_cache_test.cc)
target_link_libraries(report_test PRIVATE cc_report third_party::gtest)
add_test(NAME report_test COMMAND report_test)
