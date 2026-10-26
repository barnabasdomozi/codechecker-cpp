// Running tasks in a pool of threads, the port's replacement of Python's
// process pools (libraries.md, "Threads, pools and timers").

#ifndef CC_UTIL_PARALLEL_H_
#define CC_UTIL_PARALLEL_H_

#include <stddef.h>

#include <functional>

namespace cc::util {

// Calls `task(i)` for every i in [0, count) on `jobs` threads and returns
// when all calls returned. Tasks are started in index order (Python:
// Pool(jobs).map()).
void ParallelFor(int jobs, size_t count,
                 const std::function<void(size_t index)>& task);

}  // namespace cc::util

#endif  // CC_UTIL_PARALLEL_H_
