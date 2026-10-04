#include "net/sha1.hpp"

#include <bit>
#include <vector>

namespace mob_survivor::net {

std::array<std::uint8_t, 20> sha1(std::string_view data) {
  std::uint32_t h0 = 0x67452301U;
  std::uint32_t h1 = 0xEFCDAB89U;
  std::uint32_t h2 = 0x98BADCFEU;
  std::uint32_t h3 = 0x10325476U;
  std::uint32_t h4 = 0xC3D2E1F0U;

  std::vector<std::uint8_t> message(data.begin(), data.end());
  const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * 8U;
  message.push_back(0x80U);
  while (message.size() % 64U != 56U) message.push_back(0U);
  for (int shift = 56; shift >= 0; shift -= 8) {
    message.push_back(static_cast<std::uint8_t>(bit_length >> static_cast<unsigned>(shift)));
  }

  std::array<std::uint32_t, 80> w{};
  for (std::size_t chunk = 0; chunk < message.size(); chunk += 64U) {
    for (std::size_t i = 0; i < 16U; ++i) {
      const std::size_t at = chunk + i * 4U;
      w[i] = (static_cast<std::uint32_t>(message[at]) << 24U) | (static_cast<std::uint32_t>(message[at + 1]) << 16U) |
             (static_cast<std::uint32_t>(message[at + 2]) << 8U) | static_cast<std::uint32_t>(message[at + 3]);
    }
    for (std::size_t i = 16; i < 80U; ++i) w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    std::uint32_t a = h0;
    std::uint32_t b = h1;
    std::uint32_t c = h2;
    std::uint32_t d = h3;
    std::uint32_t e = h4;
    for (std::size_t i = 0; i < 80U; ++i) {
      std::uint32_t f = 0;
      std::uint32_t k = 0;
      if (i < 20U) {
        f = (b & c) | (~b & d);
        k = 0x5A827999U;
      } else if (i < 40U) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1U;
      } else if (i < 60U) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCU;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6U;
      }
      const std::uint32_t next = std::rotl(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = std::rotl(b, 30);
      b = a;
      a = next;
    }
    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }

  std::array<std::uint8_t, 20> digest{};
  const std::array<std::uint32_t, 5> words{h0, h1, h2, h3, h4};
  for (std::size_t i = 0; i < words.size(); ++i) {
    digest[i * 4U] = static_cast<std::uint8_t>(words[i] >> 24U);
    digest[i * 4U + 1U] = static_cast<std::uint8_t>(words[i] >> 16U);
    digest[i * 4U + 2U] = static_cast<std::uint8_t>(words[i] >> 8U);
    digest[i * 4U + 3U] = static_cast<std::uint8_t>(words[i]);
  }
  return digest;
}

}  // namespace mob_survivor::net
