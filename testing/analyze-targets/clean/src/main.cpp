#include "tokenizer.h"
#include "word_count.h"

#include <iostream>
#include <iterator>
#include <string>

int main() {
  const std::string text((std::istreambuf_iterator<char>(std::cin)),
                         std::istreambuf_iterator<char>());

  wordstat::WordCount count;
  count.add(wordstat::tokenize(text));

  std::cout << count.total() << " words, " << count.distinct()
            << " distinct\n";
  for (const auto &[word, n] : count.top(5))
    std::cout << word << ' ' << n << '\n';
  return 0;
}
