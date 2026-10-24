#!/bin/sh
# Builds every source with its own compiler call. Paths with spaces and
# non-ASCII characters are quoted, as a careful build script would do.
set -e
cd "$(dirname "$0")"
mkdir -p "out dir"
g++ -std=c++17 -I"include dir" -c "src dir/main file.cpp" -o "out dir/main file.o"
g++ -std=c++17 -I"include dir" -c "src dir/ünïcødé/naïve.cpp" -o "out dir/naïve.o"
g++ -std=c++17 -I"include dir" -DGREETING='"hello world"' -c "src dir/quote's.cpp" -o "out dir/quote's.o"
g++ -o "out dir/odd app" "out dir/main file.o" "out dir/naïve.o" "out dir/quote's.o"
