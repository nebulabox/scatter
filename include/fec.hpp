
#pragma once
#include <cstdint>
#include <vector>
#include <optional>

namespace scatter {

struct ShardPlan {
    uint16_t shard_count;
    uint16_t parity_count;
    uint32_t shard_size;
    uint16_t copies_per_shard{1};
};

class SimpleFEC {
public:
    explicit SimpleFEC(const ShardPlan& plan) : plan_(plan) {}
    void encode(const std::vector<uint8_t>& data,
                std::vector<std::vector<uint8_t>>& data_shards,
                std::vector<std::vector<uint8_t>>& parity_shards) const;
    std::optional<std::vector<uint8_t>> recover_one(
        const std::vector<std::vector<uint8_t>>& present_data,
        const std::vector<bool>& present_mask,
        const std::vector<std::vector<uint8_t>>& parity_shards) const;
private:
    ShardPlan plan_;
};

} // namespace scatter
