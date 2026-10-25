// Clang-Tidy findings, some with fix-its, from different profiles.
#include <string>
#include <utility>
#include <vector>

// default profile: misc-redundant-expression
bool same(int a) { return a == a; }

// default profile, has a fix-it: performance-move-const-arg
std::string copy_const(const std::string &text) {
  const std::string local = text;
  return std::string(std::move(local));
}

// sensitive profile, has a fix-it: performance-unnecessary-value-param
std::size_t by_value(std::vector<std::string> names) { return names.size(); }

// sensitive profile, has a fix-it: performance-for-range-copy
std::size_t range_copy(const std::vector<std::string> &names) {
  std::size_t total = 0;
  for (const std::string name : names)
    total += name.size();
  return total;
}

// extreme profile, has a fix-it: modernize-use-nullptr
int *null_literal() { return 0; }

// default profile: deadcode.DeadStores (Clang SA)
int dead_store(int a) {
  int unused_result = a * 2;
  unused_result = 0;
  return a;
}
