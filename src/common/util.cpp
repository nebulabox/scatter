
#include "util.hpp"
#include <sstream>

namespace scatter {

bool parse_host_port(const std::string &s, std::string &host, uint16_t &port) {
  auto pos = s.rfind(':');
  if (pos == std::string::npos)
    return false;
  host = s.substr(0, pos);
  try {
    int p = std::stoi(s.substr(pos + 1));
    if (p < 0 || p > 65535)
      return false;
    port = (uint16_t)p;
    return true;
  } catch (...) {
    return false;
  }
}

std::vector<uint8_t> hex_to_bytes(const std::string &hex) {
  std::vector<uint8_t> out;
  if (hex.empty() || (hex.size() % 2) != 0)
    return out;
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    unsigned int v;
    std::stringstream ss;
    ss << std::hex << hex.substr(i, 2);
    ss >> v;
    out.push_back((uint8_t)v);
  }
  return out;
}

} // namespace scatter
