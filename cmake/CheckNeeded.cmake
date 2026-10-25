# Fails if a program needs a shared library other than glibc's, libstdc++ or
# libgcc_s (docs/implementation/build-system.md §4.2). Sanitizer builds may
# also need the compiler's sanitizer runtimes.
#
#   cmake -DFILES=<program;...> -P cmake/CheckNeeded.cmake

string(CONCAT allowed
  "^(ld-linux.*|lib(c|m|dl|pthread|rt|resolv|stdc\\+\\+|gcc_s"
  "|asan|ubsan|lsan|tsan))\\.so")
foreach(file IN LISTS FILES)
  execute_process(COMMAND readelf -d ${file}
                  OUTPUT_VARIABLE dynamic COMMAND_ERROR_IS_FATAL ANY)
  string(REGEX MATCHALL "Shared library: \\[[^]]+\\]" needed "${dynamic}")
  foreach(entry IN LISTS needed)
    string(REGEX REPLACE "Shared library: \\[(.+)\\]" "\\1" library
           "${entry}")
    if(NOT library MATCHES "${allowed}")
      message(FATAL_ERROR "${file} needs ${library}")
    endif()
  endforeach()
endforeach()
