#ifndef INVENTORY_ITEM_H
#define INVENTORY_ITEM_H

#include <string>

namespace inventory {

struct Item {
  std::string name;
  int count;
  int pack_size;
};

// Defined in the header, so every source file that calls it analyzes it.
inline int full_packs(const Item &item) {
  int packs = 0;
  if (item.count > 0)
    packs = item.count / item.pack_size;
  return packs;
}

Item make_item(const std::string &name, int count);

} // namespace inventory

#endif
