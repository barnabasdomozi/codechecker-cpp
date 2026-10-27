// The map type of the generated code of the API (build-system.md §4.3):
// cmake/ThriftGenerate.cmake makes every Thrift map an OrderedMap.

#ifndef CC_API_ORDERED_MAP_H_
#define CC_API_ORDERED_MAP_H_

#include <stddef.h>

#include <map>
#include <ostream>
#include <utility>
#include <vector>

#include <thrift/TToString.h>

namespace cc::api {

// A map that keeps its keys in the order they were first added, like a
// Python dict. The API's maps are OrderedMaps, so a reply lists its entries
// in the order the handler added them, as the replies of the Python server
// do. Keys must not be changed through the iterators. Copyable.
//
//   OrderedMap<std::string, int64_t> counts;
//   counts["b"] = 1;
//   counts["a"] += 2;
//   for (const auto& [key, count] : counts) Use(key);  // "b", then "a".
template <typename Key, typename Value>
class OrderedMap {
 public:
  using value_type = std::pair<Key, Value>;
  using iterator = typename std::vector<value_type>::iterator;
  using const_iterator = typename std::vector<value_type>::const_iterator;

  OrderedMap() = default;

  OrderedMap(const OrderedMap& other) = default;
  OrderedMap& operator=(const OrderedMap& other) = default;
  OrderedMap(OrderedMap&& other) = default;
  OrderedMap& operator=(OrderedMap&& other) = default;

  iterator begin() { return entries_.begin(); }
  iterator end() { return entries_.end(); }
  const_iterator begin() const { return entries_.begin(); }
  const_iterator end() const { return entries_.end(); }
  size_t size() const { return entries_.size(); }
  bool empty() const { return entries_.empty(); }

  void clear() {
    entries_.clear();
    index_.clear();
  }

  // Returns the value of `key`; a missing key is added at the end with a
  // default value (as std::map::operator[] does).
  Value& operator[](const Key& key);

  // Returns the entry of `key`; end() if there is none.
  iterator find(const Key& key);
  const_iterator find(const Key& key) const;

 private:
  std::vector<value_type> entries_;
  std::map<Key, size_t> index_;  // The position of each key in entries_.
};

// Returns whether `a` and `b` have the same keys with equal values, in any
// order (Python: dict ==).
template <typename Key, typename Value>
bool operator==(const OrderedMap<Key, Value>& a,
                const OrderedMap<Key, Value>& b);

// Writes `map` as Thrift writes a std::map: "{<key>: <value>, ...}", for
// the generated printTo() functions.
template <typename Key, typename Value>
std::ostream& operator<<(std::ostream& out, const OrderedMap<Key, Value>& map);

// Implementation details only below here.

template <typename Key, typename Value>
Value& OrderedMap<Key, Value>::operator[](const Key& key) {
  const auto [position, added] = index_.try_emplace(key, entries_.size());
  if (added) entries_.emplace_back(key, Value());
  return entries_[position->second].second;
}

template <typename Key, typename Value>
typename OrderedMap<Key, Value>::iterator OrderedMap<Key, Value>::find(
    const Key& key) {
  const auto position = index_.find(key);
  return position == index_.end() ? end() : begin() + position->second;
}

template <typename Key, typename Value>
typename OrderedMap<Key, Value>::const_iterator OrderedMap<Key, Value>::find(
    const Key& key) const {
  const auto position = index_.find(key);
  return position == index_.end() ? end() : begin() + position->second;
}

template <typename Key, typename Value>
bool operator==(const OrderedMap<Key, Value>& a,
                const OrderedMap<Key, Value>& b) {
  if (a.size() != b.size()) return false;
  for (const auto& [key, value] : a) {
    const auto other = b.find(key);
    if (other == b.end() || !(other->second == value)) return false;
  }
  return true;
}

template <typename Key, typename Value>
std::ostream& operator<<(std::ostream& out,
                         const OrderedMap<Key, Value>& map) {
  return out << "{" << apache::thrift::to_string(map.begin(), map.end())
             << "}";
}

}  // namespace cc::api

#endif  // CC_API_ORDERED_MAP_H_
