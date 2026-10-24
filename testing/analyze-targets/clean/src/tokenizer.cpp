#include "tokenizer.h"

#include <cctype>

namespace wordstat {

std::vector<std::string> tokenize(const std::string &text) {
  std::vector<std::string> words;
  std::string current;
  for (char c : text) {
    const auto uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc)) {
      current.push_back(static_cast<char>(std::tolower(uc)));
    } else if (!current.empty()) {
      words.push_back(current);
      current.clear();
    }
  }
  if (!current.empty())
    words.push_back(current);
  return words;
}

} // namespace wordstat
