#include "util/print.h"

#include <cstdio>
#include <string_view>

namespace cc::util {
namespace {

std::FILE* File(Stream stream) {
  return stream == Stream::kStdout ? stdout : stderr;
}

}  // namespace

void Write(Stream stream, std::string_view text) {
  std::fwrite(text.data(), 1, text.size(), File(stream));
}

void Print(std::string_view text) {
  Write(Stream::kStdout, text);
  Write(Stream::kStdout, "\n");
}

void Flush(Stream stream) { std::fflush(File(stream)); }

}  // namespace cc::util
