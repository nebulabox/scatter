
#pragma once
#include <cstdint>
#include <vector>

namespace scatter {

class CryptoProvider {
public:
    virtual ~CryptoProvider() = default;
    virtual void set_key(const std::vector<uint8_t>& key) = 0;
    virtual bool encrypt(uint64_t session_id, uint64_t block_id, uint16_t shard_id,
                         uint8_t direction, std::vector<uint8_t>& inout) = 0;
    virtual bool decrypt(uint64_t session_id, uint64_t block_id, uint16_t shard_id,
                         uint8_t direction, std::vector<uint8_t>& inout) = 0;
};

class XorStreamCipher : public CryptoProvider {
public:
    XorStreamCipher();
    void set_key(const std::vector<uint8_t>& key) override;
    bool encrypt(uint64_t session_id, uint64_t block_id, uint16_t shard_id,
                 uint8_t direction, std::vector<uint8_t>& inout) override;
    bool decrypt(uint64_t session_id, uint64_t block_id, uint16_t shard_id,
                 uint8_t direction, std::vector<uint8_t>& inout) override;
private:
    uint64_t s0_;
    uint64_t s1_;
    void reseed(uint64_t nonce);
    uint64_t next();
};

#ifdef SCATTER_HAVE_SODIUM
class SodiumAead : public CryptoProvider {
public:
    SodiumAead();
    void set_key(const std::vector<uint8_t>& key) override;
    bool encrypt(uint64_t session_id, uint64_t block_id, uint16_t shard_id,
                 uint8_t direction, std::vector<uint8_t>& inout) override;
    bool decrypt(uint64_t session_id, uint64_t block_id, uint16_t shard_id,
                 uint8_t direction, std::vector<uint8_t>& inout) override;
private:
    std::vector<uint8_t> key_;
};
#endif

} // namespace scatter
