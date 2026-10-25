#!/bin/sh
# Checks out the third-party submodules, including the nested ones the build
# needs (docs/implementation/build-system.md §2.3). Never use
# `git submodule update --recursive` instead: it fetches all of Boost and
# OpenSSL's test submodules.
#
# Usage: third_party/checkout.sh [--analyzer-only]

set -eu
cd "$(dirname "$0")/.."

case "${1:-}" in
  "") paths="" ;;
  --analyzer-only)
    paths="third_party/pcre2 third_party/yyjson third_party/libyaml
           third_party/libxml2 third_party/zlib third_party/libzip
           third_party/openssl third_party/googletest" ;;
  *) echo "usage: $0 [--analyzer-only]" >&2; exit 2 ;;
esac

git submodule sync --quiet
# $paths is split into words on purpose; empty means every submodule.
# shellcheck disable=SC2086
git submodule update --init --depth 1 --jobs 8 $paths
git -C third_party/pcre2 submodule update --init --depth 1 deps/sljit
if [ -z "$paths" ]; then
  # shellcheck disable=SC2046
  git -C third_party/boost submodule update --init --depth 1 --jobs 8 \
      $(grep -v '^#' third_party/boost-libraries.txt)
fi
