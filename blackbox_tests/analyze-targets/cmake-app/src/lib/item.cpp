#include "inventory/item.h"

namespace inventory {

Item make_item(const std::string &name, int count) {
  Item item;
  item.name = name;
  item.count = count;
  item.pack_size = 0; // a pack size is set later, but full_packs() may run first
  if (full_packs(item) > INVENTORY_VERSION)
    item.count = 0;
  return item;
}

} // namespace inventory
