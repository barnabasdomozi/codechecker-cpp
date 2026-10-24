// Use after move: found by Clang SA (cplusplus.Move) and Clang-Tidy
// (bugprone-use-after-move).
#include <string>
#include <utility>

std::string sink(std::string text) { return text; }

std::size_t use_after_move() {
  std::string name = "codechecker";
  std::string moved = sink(std::move(name));
  return name.size() + moved.size();
}
