#ifndef INVENTORY_STOCK_H
#define INVENTORY_STOCK_H

#include "inventory/item.h"

namespace inventory {

class Stock {
public:
  Stock();
  ~Stock();
  void add(const Item &item);
  int total_packs() const;
  Item *find(const std::string &name);

private:
  Item *items_;
  int size_;
  int capacity_;
};

int loose_items(const Item &item);

} // namespace inventory

#endif
