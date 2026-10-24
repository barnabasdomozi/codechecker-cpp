#include "buffer.h"

namespace core {

Buffer::Buffer(std::size_t size) : data_(new char[size]), size_(size) {}

Buffer::~Buffer() { delete[] data_; }

int Buffer::slots_per_retry() const {
  // CONFIG_SPARE_SLOTS comes from the force-included config.h.
  return CONFIG_MAX_RETRIES / CONFIG_SPARE_SLOTS;
}

#ifdef CHECKED_BUFFER
// Only compiled in the checked build (-DCHECKED_BUFFER).
char checked_at(Buffer &buffer, std::size_t index) {
  char *base = nullptr;
  if (index < buffer.size())
    base = buffer.data();
  return base[index];
}
#endif

} // namespace core
