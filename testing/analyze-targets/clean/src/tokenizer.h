#ifndef WORDSTAT_TOKENIZER_H
#define WORDSTAT_TOKENIZER_H

#include <string>
#include <vector>

namespace wordstat {

// Splits text into lower-case words. Anything that is not a letter or a
// digit separates words.
std::vector<std::string> tokenize(const std::string &text);

} // namespace wordstat

#endif
