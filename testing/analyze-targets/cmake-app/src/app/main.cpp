#include "inventory/stock.h"
#include "tinyjson.h"

#include <cstdio>

int main(int argc, char **argv) {
  inventory::Stock stock;
  stock.add(inventory::make_item("bolt", 120));
  stock.add(inventory::make_item("nut", 75));

  // The same header bug as in item.cpp, reached from a second source file.
  inventory::Item sample{"sample", 3, 0};
  std::printf("%d\n", inventory::full_packs(sample));

  int limit;
  if (argc > 2)
    limit = 10;
  std::printf("%d packs (limit %d), %s\n", stock.total_packs(), limit,
              tinyjson::quote(argc > 1 ? argv[1] : "ok"));
  return 0;
}
