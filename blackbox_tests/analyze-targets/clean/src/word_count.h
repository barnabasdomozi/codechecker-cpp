#ifndef WORDSTAT_WORD_COUNT_H
#define WORDSTAT_WORD_COUNT_H

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace wordstat {

class WordCount {
public:
  void add(const std::vector<std::string> &words);
  std::size_t total() const { return total_; }
  std::size_t distinct() const { return counts_.size(); }
  // The 'limit' most frequent words, most frequent first.
  std::vector<std::pair<std::string, std::size_t>> top(std::size_t limit) const;

private:
  std::map<std::string, std::size_t> counts_;
  std::size_t total_ = 0;
};

} // namespace wordstat

#endif
