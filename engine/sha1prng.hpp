#pragma once
// sha1prng.hpp - bit-exact port of Java's SecureRandom("SHA1PRNG") (sun.security.provider.SecureRandom),
// java.util.Random.nextInt(bound) and Collections.shuffle, as used by the CodinGame SDK
// (MultiplayerGameManager.getRandom()) and the Poker referee's Deck.  Mirrors sim/poker_sim.py.
#include <cstdint>
#include <cstring>
#include <vector>

namespace pk {

struct Sha1 {
  static void digest(const uint8_t* msg, size_t len, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::vector<uint8_t> m(msg, msg + len);
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 7; i >= 0; i--) m.push_back((uint8_t)(bits >> (8 * i)));
    for (size_t off = 0; off < m.size(); off += 64) {
      uint32_t w[80];
      for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)m[off + 4 * i] << 24 | (uint32_t)m[off + 4 * i + 1] << 16 |
               (uint32_t)m[off + 4 * i + 2] << 8 | m[off + 4 * i + 3];
      for (int i = 16; i < 80; i++) {
        uint32_t x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = x << 1 | x >> 31;
      }
      uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
      for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999u;
        else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1u;
        else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDCu;
        else f = b ^ c ^ d, k = 0xCA62C1D6u;
        uint32_t t = (a << 5 | a >> 27) + f + e + k + w[i];
        e = d; d = c; c = b << 30 | b >> 2; b = a; a = t;
      }
      h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; i++)
      out[4 * i] = h[i] >> 24, out[4 * i + 1] = h[i] >> 16, out[4 * i + 2] = h[i] >> 8, out[4 * i + 3] = h[i];
  }
};

class Sha1Prng {
 public:
  explicit Sha1Prng(int64_t seed) {
    uint8_t sb[8];                                   // SecureRandom.longToByteArray: little-endian
    for (int i = 0; i < 8; i++) sb[i] = (uint8_t)((uint64_t)seed >> (8 * i));
    Sha1::digest(sb, 8, state_);
  }
  // java.util.Random.next(bits) as implemented by SHA1PRNG.engineNextBytes
  uint32_t next(int bits) {
    int nb = (bits + 7) / 8;
    uint64_t v = 0;
    for (int i = 0; i < nb; i++) v = v << 8 | next_byte();
    return (uint32_t)(v >> (nb * 8 - bits));
  }
  int next_int(int bound) {                          // java.util.Random.nextInt(bound)
    uint32_t r = next(31);
    int m = bound - 1;
    if ((bound & m) == 0) return (int)(((int64_t)bound * (int64_t)r) >> 31);
    int32_t u = (int32_t)r;
    for (;;) {
      int32_t rr = u % bound;
      if ((int64_t)u - rr + m < (1LL << 31)) return rr;
      u = (int32_t)next(31);
    }
  }
  template <class T>
  void shuffle(std::vector<T>& v) {                   // Collections.shuffle, RandomAccess branch
    for (int i = (int)v.size(); i > 1; i--) {
      int j = next_int(i);
      std::swap(v[i - 1], v[j]);
    }
  }

 private:
  uint8_t state_[20];
  uint8_t buf_[20];
  int buf_pos_ = 20;
  uint8_t next_byte() {
    if (buf_pos_ == 20) next_block(), buf_pos_ = 0;
    return buf_[buf_pos_++];
  }
  void next_block() {
    Sha1::digest(state_, 20, buf_);
    // updateState: Java adds *signed* bytes; the carry is v >> 8 and can be -1
    int last = 1;
    bool zf = false;
    for (int i = 0; i < 20; i++) {
      int sv = (int8_t)state_[i], ov = (int8_t)buf_[i];
      int v = sv + ov + last;
      uint8_t t = (uint8_t)(v & 0xFF);
      zf |= state_[i] != t;
      state_[i] = t;
      last = v >> 8;
    }
    if (!zf) state_[0]++;
  }
};

}  // namespace pk
