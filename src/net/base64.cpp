#include "net/base64.hpp"

#include <cstdint>

namespace mob_survivor::net {

std::string base64_encode(std::string_view bytes) {
  static constexpr std::string_view kAlphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((bytes.size() + 2U) / 3U * 4U);
  std::size_t i = 0;
  for (; i + 2U < bytes.size(); i += 3U) {
    const std::uint32_t n = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[i])) << 16U) |
                            (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[i + 1])) << 8U) |
                            static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[i + 2]));
    out.push_back(kAlphabet[(n >> 18U) & 63U]);
    out.push_back(kAlphabet[(n >> 12U) & 63U]);
    out.push_back(kAlphabet[(n >> 6U) & 63U]);
    out.push_back(kAlphabet[n & 63U]);
  }
  const std::size_t rest = bytes.size() - i;
  if (rest > 0U) {
    std::uint32_t n = static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[i])) << 16U;
    if (rest == 2U) n |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[i + 1])) << 8U;
    out.push_back(kAlphabet[(n >> 18U) & 63U]);
    out.push_back(kAlphabet[(n >> 12U) & 63U]);
    out.push_back(rest == 2U ? kAlphabet[(n >> 6U) & 63U] : '=');
    out.push_back('=');
  }
  return out;
}

}  // namespace mob_survivor::net
