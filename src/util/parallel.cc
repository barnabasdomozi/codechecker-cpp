#include "util/parallel.h"

#include <stddef.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <thread>
#include <vector>

namespace cc::util {

void ParallelFor(int jobs, size_t count,
                 const std::function<void(size_t index)>& task) {
  std::atomic<size_t> next = 0;
  const auto work = [&next, count, &task] {
    for (size_t i = next.fetch_add(1); i < count; i = next.fetch_add(1)) {
      task(i);
    }
  };
  const size_t threads =
      std::min(static_cast<size_t>(std::max(jobs, 1)), count);
  if (threads <= 1) {
    work();
    return;
  }
  std::vector<std::jthread> pool;
  for (size_t i = 0; i < threads; ++i) pool.emplace_back(work);
}

}  // namespace cc::util
