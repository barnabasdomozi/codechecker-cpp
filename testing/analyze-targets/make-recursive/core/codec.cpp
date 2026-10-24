#include "buffer.h"

namespace core {

int encode(Buffer &buffer, int value) {
  int shift = 0;
#ifdef CODEC_FAST
  // Only in the fast variant (codec_fast.o).
  int *table = nullptr;
  shift = table[value & 3];
#endif
  int scale = 0;
  // In both variants.
  return (value << shift) / scale + static_cast<int>(buffer.size());
}

} // namespace core
