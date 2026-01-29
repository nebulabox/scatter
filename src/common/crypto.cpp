
#include "crypto.hpp"
#include <cstring>

#ifdef SCATTER_HAVE_SODIUM
#include <sodium.h>
#endif

namespace scatter {

XorStreamCipher::XorStreamCipher() {
  s0_ = 0x123456789abcdef0ull;
  s1_ = 0x0fedcba987654321ull;
}

void XorStreamCipher::set_key(const std::vector<uint8_t> &key) {
  uint64_t a = 0x243f6a8885a308d3ull, b = 0x13198a2e03707344ull;
  for (size_t i = 0; i < key.size(); ++i) {
    a ^= (uint64_t)key[i] << ((i % 8) * 8);
    a = (a << 7) | (a >> 57);
    b ^= (uint64_t)key[i] << (((i + 3) % 8) * 8);
    b = (b << 11) | (b >> 53);
  }
  s0_ = a;
  s1_ = b;
  if (s0_ == 0 && s1_ == 0) {
    s0_ = 1;
  }
}

void XorStreamCipher::reseed(uint64_t nonce) {
  s0_ ^= nonce | 1ull;
  s1_ ^= (nonce << 1) | 1ull;
  for (int i = 0; i < 8; i++)
    (void)next();
}

uint64_t XorStreamCipher::next() {
  uint64_t x = s0_;
  uint64_t y = s1_;
  s0_ = y;
  x ^= x << 23;
  x ^= x >> 17;
  x ^= y ^ (y >> 26);
  s1_ = x;
  return x + y;
}

bool XorStreamCipher::encrypt(uint64_t session_id, uint64_t block_id,
                              uint16_t shard_id, uint8_t direction,
                              std::vector<uint8_t> &inout) {
  uint64_t nonce = session_id ^ (block_id * 0x9e3779b97f4a7c15ull) ^
                   ((uint64_t)shard_id << 32) ^ (uint64_t)direction;
  uint64_t old_s0 = s0_, old_s1 = s1_;
  reseed(nonce);
  size_t i = 0;
  size_t n = inout.size();
  while (i < n) {
    uint64_t k = next();
    for (int j = 0; j < 8 && i < n; j++, i++)
      inout[i] ^= ((uint8_t *)&k)[j];
  }
  s0_ = old_s0;
  s1_ = old_s1;
  return true;
}

bool XorStreamCipher::decrypt(uint64_t session_id, uint64_t block_id,
                              uint16_t shard_id, uint8_t direction,
                              std::vector<uint8_t> &inout) {
  return encrypt(session_id, block_id, shard_id, direction, inout);
}

#ifdef SCATTER_HAVE_SODIUM
static void
derive_nonce(uint8_t nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES],
             uint64_t sid, uint64_t bid, uint16_t shard, uint8_t dir) {
  uint8_t buf[8 + 8 + 2 + 1]{};
  std::memcpy(buf + 0, &sid, 8);
  std::memcpy(buf + 8, &bid, 8);
  std::memcpy(buf + 16, &shard, 2);
  buf[18] = dir;
  crypto_generichash(nonce, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, buf,
                     sizeof(buf), nullptr, 0);
}

SodiumAead::SodiumAead() {
  if (sodium_init() < 0) {
  }
}

void SodiumAead::set_key(const std::vector<uint8_t> &key) {
  key_.assign(32, 0);
  if (key.size() == 32)
    key_ = key;
  else if (!key.empty()) {
    key_.resize(32);
    crypto_generichash(key_.data(), key_.size(), key.data(), key.size(),
                       nullptr, 0);
  }
}

bool SodiumAead::encrypt(uint64_t session_id, uint64_t block_id,
                         uint16_t shard_id, uint8_t direction,
                         std::vector<uint8_t> &inout) {
  uint8_t nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
  derive_nonce(nonce, session_id, block_id, shard_id, direction);
  size_t clen = inout.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES;
  std::vector<uint8_t> out(clen);
  unsigned long long outlen = 0;
  if (crypto_aead_xchacha20poly1305_ietf_encrypt(
          out.data(), &outlen, inout.data(), inout.size(), nullptr, 0, nullptr,
          nonce, key_.data()) != 0)
    return false;
  out.resize((size_t)outlen);
  inout.swap(out);
  return true;
}

bool SodiumAead::decrypt(uint64_t session_id, uint64_t block_id,
                         uint16_t shard_id, uint8_t direction,
                         std::vector<uint8_t> &inout) {
  uint8_t nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
  derive_nonce(nonce, session_id, block_id, shard_id, direction);
  std::vector<uint8_t> out(inout.size());
  unsigned long long outlen = 0;
  if (crypto_aead_xchacha20poly1305_ietf_decrypt(
          out.data(), &outlen, nullptr, inout.data(), inout.size(), nullptr, 0,
          nonce, key_.data()) != 0)
    return false;
  out.resize((size_t)outlen);
  inout.swap(out);
  return true;
}
#endif

} // namespace scatter
