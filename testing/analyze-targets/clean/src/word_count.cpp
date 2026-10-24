#include "word_count.h"

#include <algorithm>

namespace wordstat {

void WordCount::add(const std::vector<std::string> &words) {
  for (const auto &word : words)
    ++counts_[word];
  total_ += words.size();
}

std::vector<std::pair<std::string, std::size_t>>
WordCount::top(std::size_t limit) const {
  std::vector<std::pair<std::string, std::size_t>> result(counts_.begin(),
                                                          counts_.end());
  std::stable_sort(result.begin(), result.end(),
                   [](const auto &a, const auto &b) { return a.second > b.second; });
  if (result.size() > limit)
    result.resize(limit);
  return result;
}

} // namespace wordstat
