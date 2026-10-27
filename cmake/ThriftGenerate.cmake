# Generates code from the Thrift IDL files in api/ (build-system.md §4.3).
#
#   cmake -DTHRIFT=<thrift compiler> -DIDL_DIR=<api folder>
#         -DOUT_DIR=<output folder> -DLANGUAGE=cpp|js
#         -P cmake/ThriftGenerate.cmake
#
# cpp: the C++ code of every IDL file, for cc_api. Two steps make the
# generated code behave like the Python stubs, without changing the IDL
# files:
#   - The C++ generator writes the structs of a file in the order they are
#     declared, but report_server.thrift uses some structs before it declares
#     them. The compiler reads a copy in which those structs come first; field
#     ids and types stay the same, so the wire format does not change.
#   - The C++ generator always sends the fields that are not marked
#     `optional`, while Python sends only the fields that are not None. The
#     struct writers are changed to send such a field only if it is set
#     (`__set_<field>()`, or read from a message), as Python does, and the
#     setters of such fields mark them as set.
#   - The C++ generator makes Thrift maps std::maps, which send their
#     entries sorted, while Python dicts send them in the order they were
#     added. Thrift maps become cc::api::OrderedMaps (src/api/ordered_map.h),
#     which keep that order.
#
# js: the JavaScript stubs of the web UI, as codechecker_api/Makefile
# generates them in the Python project.

if(LANGUAGE STREQUAL "js")
  file(MAKE_DIRECTORY ${OUT_DIR})
  file(GLOB idl_files ${IDL_DIR}/*.thrift)
  foreach(idl IN LISTS idl_files)
    execute_process(
      COMMAND ${THRIFT} -r --gen js:node -out ${OUT_DIR} ${idl}
      RESULT_VARIABLE result ERROR_VARIABLE errors)
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "thrift failed on ${idl}:\n${errors}")
    endif()
  endforeach()
  return()
endif()

# The structs that report_server.thrift uses before declaring them, in an
# order in which each one is declared before it is used.
set(early_structs DateInterval BlameData CommitAuthor Commit BlameInfo)

set(idl_copy_dir ${OUT_DIR}/idl)
file(MAKE_DIRECTORY ${idl_copy_dir})
file(GLOB idl_files ${IDL_DIR}/*.thrift)
foreach(idl IN LISTS idl_files)
  get_filename_component(name ${idl} NAME)
  file(READ ${idl} text)
  if(name STREQUAL "report_server.thrift")
    set(moved "")
    foreach(struct IN LISTS early_structs)
      string(REGEX MATCH "struct ${struct} {[^}]*}\n" block "${text}")
      if(NOT block)
        message(FATAL_ERROR "struct ${struct} not found in ${idl}")
      endif()
      string(REPLACE "${block}" "" text "${text}")
      string(APPEND moved "${block}\n")
    endforeach()
    string(FIND "${text}" "\nstruct " first_struct)
    math(EXPR rest_start "${first_struct} + 1")
    string(SUBSTRING "${text}" 0 ${rest_start} head)
    string(SUBSTRING "${text}" ${rest_start} -1 rest)
    set(text "${head}${moved}${rest}")
  endif()
  file(WRITE ${idl_copy_dir}/${name} "${text}")
endforeach()

file(GLOB idl_copies ${idl_copy_dir}/*.thrift)
foreach(idl IN LISTS idl_copies)
  execute_process(
    COMMAND ${THRIFT} --gen cpp:no_skeleton -out ${OUT_DIR} ${idl}
    RESULT_VARIABLE result ERROR_VARIABLE errors)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "thrift failed on ${idl}:\n${errors}")
  endif()
endforeach()

# The std::maps of the generated code that are not Thrift maps: the names
# of enum values and the method tables of the processors.
set(std_maps "std::map<int, const char*>"
             "std::map<std::string, ProcessFunction>")
file(GLOB generated_files ${OUT_DIR}/*.h ${OUT_DIR}/*.cpp)
foreach(file IN LISTS generated_files)
  file(READ ${file} text)
  set(index 0)
  foreach(std_map IN LISTS std_maps)
    string(REPLACE "${std_map}" "@std_map_${index}@" text "${text}")
    math(EXPR index "${index} + 1")
  endforeach()
  string(REPLACE "std::map<" "::cc::api::OrderedMap<" text "${text}")
  set(index 0)
  foreach(std_map IN LISTS std_maps)
    string(REPLACE "@std_map_${index}@" "${std_map}" text "${text}")
    math(EXPR index "${index} + 1")
  endforeach()
  # Every other generated header includes a *_types.h header.
  if(file MATCHES "_types\\.h$")
    string(REPLACE "#include <thrift/Thrift.h>"
                   "#include <thrift/Thrift.h>\n#include \"api/ordered_map.h\""
                   text "${text}")
  endif()
  file(WRITE ${file} "${text}")
endforeach()

# In the struct writers, the fields that are not `optional` are the ones at
# the first indentation level; the generator already guards the others with
# `if (this->__isset.<field>) {`.
file(GLOB type_sources ${OUT_DIR}/*_types.cpp)
foreach(source IN LISTS type_sources)
  file(READ ${source} text)
  string(REGEX REPLACE
    "\n  xfer \\+= oprot->writeFieldBegin\\(\"([A-Za-z0-9_]+)\""
    "\n  if (this->__isset.\\1) {\n  xfer += oprot->writeFieldBegin(\"\\1\""
    text "${text}")
  string(REPLACE
    "\n  xfer += oprot->writeFieldEnd();"
    "\n  xfer += oprot->writeFieldEnd();\n  }"
    text "${text}")
  # The setters of such fields mark them as set, as those of the optional
  # fields do.
  string(REGEX REPLACE
    "(::__set_([A-Za-z0-9_]+)\\([^)]*\\) {\n  this->[A-Za-z0-9_]+ = val;\n)}"
    "\\1__isset.\\2 = true;\n}"
    text "${text}")
  file(WRITE ${source} "${text}")
endforeach()
