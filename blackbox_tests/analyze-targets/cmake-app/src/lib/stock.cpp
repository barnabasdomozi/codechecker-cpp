#include "inventory/stock.h"
#include "tinyjson.h"

namespace inventory {

Stock::Stock() : items_(new Item[4]), size_(0), capacity_(4) {}

Stock::~Stock() { delete[] items_; }

void Stock::add(const Item &item) {
  if (size_ == capacity_) {
    Item *bigger = new Item[capacity_ * 2];
    for (int i = 0; i < size_; ++i)
      bigger[i] = items_[i];
    if (bigger[0].count < 0)
      return; // 'bigger' is leaked here
    delete[] items_;
    items_ = bigger;
    capacity_ *= 2;
  }
  items_[size_++] = item;
}

int Stock::total_packs() const {
  int total = 0;
  for (int i = 0; i < size_; ++i)
    total += full_packs(items_[i]);
  return total;
}

Item *Stock::find(const std::string &name) {
  for (int i = 0; i < size_; ++i)
    if (items_[i].name == name)
      return &items_[i];
  return nullptr;
}

int loose_items(const Item &item) {
  Item copy = item;
  copy.pack_size = 0;
  return copy.count % copy.pack_size;
}

} // namespace inventory
