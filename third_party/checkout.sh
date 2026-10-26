#!/bin/sh
# Checks out the third-party submodules, including the nested ones the build
# needs (docs/implementation/build-system.md §2.3). Never use
# `git submodule update --recursive` instead: it fetches all of Boost and
# OpenSSL's test submodules.
#
# Usage: third_party/checkout.sh

set -eu
cd "$(dirname "$0")/.."

if [ $# -ne 0 ]; then
  echo "usage: $0" >&2
  exit 2
fi

git submodule sync --quiet
git submodule update --init --depth 1 --jobs 8
git -C third_party/pcre2 submodule update --init --depth 1 deps/sljit
# shellcheck disable=SC2046
git -C third_party/boost submodule update --init --depth 1 --jobs 8 \
    $(grep -v '^#' third_party/boost-libraries.txt)
