
#pragma once
#include <string>
#include <cstdint>
#include <vector>

namespace scatter {

bool parse_host_port(const std::string& s, std::string& host, uint16_t& port);
std::vector<uint8_t> hex_to_bytes(const std::string& hex);

} // namespace scatter
