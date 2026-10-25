# Writes the version files of the package (build-system.md §4.3) with the
# git information and build date that scripts/build/extend_version_file.py
# adds in the Python build. A file is rewritten only when its content
# changes.
#
#   cmake -DSOURCE_DIR=<repository> -DOUTPUTS=<file;...>
#         -DVERSION_MAJOR=6 -DVERSION_MINOR=30 -DVERSION_REVISION=0
#         -DVERSION_RC= -P cmake/VersionFiles.cmake

# The short version string of the Python script: "6.30" for 6.30.0.
set(version_string ${VERSION_MAJOR})
if(NOT VERSION_MINOR EQUAL 0 OR NOT VERSION_REVISION EQUAL 0)
  string(APPEND version_string .${VERSION_MINOR})
endif()
if(NOT VERSION_REVISION EQUAL 0)
  string(APPEND version_string .${VERSION_REVISION})
endif()
if(NOT VERSION_RC STREQUAL "" AND NOT VERSION_RC EQUAL 0)
  string(APPEND version_string -rc${VERSION_RC})
endif()

function(run_git output)
  execute_process(COMMAND git ${ARGN} WORKING_DIRECTORY ${SOURCE_DIR}
                  OUTPUT_VARIABLE result OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)
  set(${output} "${result}" PARENT_SCOPE)
endfunction()

# The members in the order of json.dumps(sort_keys=True, indent=4).
set(git_members "")
if(EXISTS ${SOURCE_DIR}/.git)
  run_git(git_hash rev-parse HEAD)
  run_git(describe describe --always --tags --abbrev=0)
  run_git(describe_dirty describe --always --tags --dirty=-tainted)
  # The tag is always the configured version; the dirty tag keeps git's
  # suffix (commits since the tag, abbreviated hash, "-tainted").
  set(dirty_tag "${describe_dirty}")
  if(NOT describe STREQUAL "")
    string(REPLACE "${describe}" "${version_string}" dirty_tag
           "${describe_dirty}")
  endif()
  string(CONCAT git_members
    "    \"git_describe\": {\n"
    "        \"dirty\": \"${dirty_tag}\",\n"
    "        \"tag\": \"${version_string}\"\n"
    "    },\n"
    "    \"git_hash\": \"${git_hash}\",\n")
endif()
string(TIMESTAMP build_date "%Y-%m-%dT%H:%M")
string(CONCAT content
  "{\n"
  "${git_members}"
  "    \"package_build_date\": \"${build_date}\",\n"
  "    \"version\": {\n"
  "        \"major\": \"${VERSION_MAJOR}\",\n"
  "        \"minor\": \"${VERSION_MINOR}\",\n"
  "        \"rc\": \"${VERSION_RC}\",\n"
  "        \"revision\": \"${VERSION_REVISION}\"\n"
  "    }\n"
  "}")

foreach(output IN LISTS OUTPUTS)
  file(WRITE ${output}.tmp "${content}")
  file(COPY_FILE ${output}.tmp ${output} ONLY_IF_DIFFERENT)
  file(REMOVE ${output}.tmp)
endforeach()
