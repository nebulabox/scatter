
#include "fec.hpp"
#include <algorithm>
#include <cstring>

namespace scatter {

void SimpleFEC::encode(const std::vector<uint8_t> &data,
                       std::vector<std::vector<uint8_t>> &data_shards,
                       std::vector<std::vector<uint8_t>> &parity_shards) const {
  data_shards.clear();
  parity_shards.clear();
  size_t total = data.size();
  size_t shard_size = plan_.shard_size;
  size_t shards = plan_.shard_count;
  data_shards.resize(shards, std::vector<uint8_t>(shard_size, 0));

  size_t offset = 0;
  for (size_t i = 0; i < shards && offset < total; i++) {
    size_t n = std::min(shard_size, total - offset);
    std::memcpy(data_shards[i].data(), data.data() + offset, n);
    offset += n;
  }

  if (plan_.parity_count > 0) {
    std::vector<uint8_t> p(shard_size, 0);
    for (size_t i = 0; i < shards; i++) {
      for (size_t j = 0; j < shard_size; j++)
        p[j] ^= data_shards[i][j];
    }
    parity_shards.push_back(std::move(p));
  }
}

std::optional<std::vector<uint8_t>> SimpleFEC::recover_one(
    const std::vector<std::vector<uint8_t>> &present_data,
    const std::vector<bool> &present_mask,
    const std::vector<std::vector<uint8_t>> &parity_shards) const {
  if (plan_.parity_count == 0 || parity_shards.empty())
    return std::nullopt;
  size_t missing_cnt = 0, miss_idx = 0;
  for (size_t i = 0; i < present_mask.size(); ++i) {
    if (!present_mask[i]) {
      missing_cnt++;
      miss_idx = i;
    }
  }
  if (missing_cnt != 1)
    return std::nullopt;
  std::vector<uint8_t> rec(plan_.shard_size, 0);
  for (size_t j = 0; j < plan_.shard_size; j++)
    rec[j] ^= parity_shards[0][j];
  for (size_t i = 0; i < present_data.size(); ++i) {
    if (present_mask[i]) {
      for (size_t j = 0; j < plan_.shard_size; j++)
        rec[j] ^= present_data[i][j];
    }
  }
  return rec;
}

} // namespace scatter
