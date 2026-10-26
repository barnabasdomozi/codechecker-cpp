# Unit tests of cc_util (architecture.md §13). The expected values are the
# results of the Python functions that the code ports, in the reference
# environment (Python 3.13, prettytable 3.18.0).

add_executable(util_test
  argparse_test.cc
  fnmatch_test.cc
  json_test.cc
  plist_test.cc
  pypath_test.cc
  pyre_test.cc
  shlex_test.cc
  strings_test.cc
  template_test.cc
  textwrap_test.cc
  twodim_test.cc
  yaml_test.cc)
target_link_libraries(util_test PRIVATE cc_util third_party::gtest)
add_test(NAME util_test COMMAND util_test)
