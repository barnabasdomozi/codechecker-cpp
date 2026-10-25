#ifndef CORE_BUFFER_H
#define CORE_BUFFER_H

#include <cstddef>

namespace core {

class Buffer {
public:
  explicit Buffer(std::size_t size);
  ~Buffer();
  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;

  char *data() { return data_; }
  std::size_t size() const { return size_; }
  int slots_per_retry() const;

private:
  char *data_;
  std::size_t size_;
};

int encode(Buffer &buffer, int value);

} // namespace core

#endif
