# Used by third_party/CMakeLists.txt and by the project: both must compute
# the same value (docs/implementation/build-system.md §3.1 and §4.1).

# Sets out_var to a hash of everything the third-party libraries are built
# from: the checked-out commit of every submodule (nested ones included) and
# the files of the deps build. Also makes the calling directory reconfigure
# when a submodule is checked out at another commit.
function(cc_deps_sources root out_var)
  execute_process(
    COMMAND git submodule status --recursive
    WORKING_DIRECTORY "${root}"
    OUTPUT_VARIABLE status
    RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "`git submodule status` failed in ${root}.")
  endif()
  # Keep the commit and the path of each line. Drop the status character
  # and the `git describe` text, which can change while the sources do not.
  string(REGEX REPLACE "(^|\n).([0-9a-f]+) ([^ \n]+)[^\n]*" "\\1\\2 \\3"
         status "${status}")
  file(SHA256 "${root}/third_party/CMakeLists.txt" deps_build_hash)
  file(SHA256 "${root}/third_party/boost-libraries.txt" boost_list_hash)
  string(SHA256 hash "${status}${deps_build_hash}${boost_list_hash}")
  set(${out_var} "${hash}" PARENT_SCOPE)

  execute_process(
    COMMAND git submodule foreach --quiet --recursive
            "git rev-parse --absolute-git-dir"
    WORKING_DIRECTORY "${root}"
    OUTPUT_VARIABLE git_dirs
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(heads)
  if(git_dirs)
    string(REPLACE "\n" "/HEAD;" heads "${git_dirs}/HEAD")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               "${root}/third_party/CMakeLists.txt"
               "${root}/third_party/boost-libraries.txt" ${heads})
endfunction()
