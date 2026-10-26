// Values computed once and remembered, for results that are expensive to
// get, such as the output of an analyzer binary.

#ifndef CC_UTIL_MEMO_H_
#define CC_UTIL_MEMO_H_

#include <functional>
#include <map>
#include <mutex>

namespace cc::util {

// A thread-safe map of computed values. A value is computed by the first
// Get() of its key; later calls return the same value.
template <typename Key, typename Value>
class Memo {
 public:
  Memo() = default;
  Memo(const Memo&) = delete;
  Memo& operator=(const Memo&) = delete;

  // Returns the value of `key`, calling `compute` if it is not known yet.
  // `compute` runs under the lock, so it must not use this Memo.
  const Value& Get(const Key& key, const std::function<Value()>& compute) {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = values_.find(key);
    if (it == values_.end()) it = values_.emplace(key, compute()).first;
    return it->second;
  }

 private:
  std::mutex mutex_;
  std::map<Key, Value> values_;
};

}  // namespace cc::util

#endif  // CC_UTIL_MEMO_H_
