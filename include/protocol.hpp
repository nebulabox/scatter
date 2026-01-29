
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace scatter {

constexpr uint32_t kMagic = 0x53435452; // 'SCTR'
constexpr uint8_t  kVersion = 1;

enum class Direction : uint8_t { Uplink = 0, Downlink = 1 };

enum FrameFlags : uint8_t {
    FF_DATA    = 0x01,
    FF_CONTROL = 0x02,
    FF_PARITY  = 0x04,
    FF_DUP     = 0x08
};

#pragma pack(push, 1)
struct FrameHeader {
    uint32_t magic;
    uint8_t  version;
    uint8_t  flags;
    uint8_t  direction;
    uint8_t  reserved;
    uint64_t session_id;
    uint64_t block_id;
    uint16_t shard_id;
    uint16_t shard_count;
    uint16_t parity_count;
    uint16_t copy_seq;
    uint32_t payload_len;
    uint32_t header_crc32;
};
#pragma pack(pop)
static_assert(sizeof(FrameHeader) == 40, "FrameHeader must be 40 bytes");

struct Frame {
    FrameHeader hdr{};
    std::vector<uint8_t> payload;
};

enum class ControlType : uint8_t {
    SESSION_OPEN = 1,
    SESSION_CLOSE = 2,
    SESSION_RESET = 3
};

uint32_t crc32(const uint8_t* data, size_t len);

} // namespace scatter
