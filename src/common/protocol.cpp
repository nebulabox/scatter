
#include "protocol.hpp"
#include <array>

namespace scatter {

uint32_t crc32(const uint8_t *data, size_t len) {
  static std::array<uint32_t, 256> table{};
  static bool inited = false;
  if (!inited) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int j = 0; j < 8; j++)
        c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    inited = true;
  }
  uint32_t c = ~0u;
  for (size_t i = 0; i < len; i++)
    c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
  return ~c;
}

} // namespace scatter
