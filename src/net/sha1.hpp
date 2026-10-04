#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace mob_survivor::net {

// SHA-1 (FIPS 180-4). Used only for the WebSocket handshake (RFC 6455), which
// requires it; never for anything that needs collision resistance.
[[nodiscard]] std::array<std::uint8_t, 20> sha1(std::string_view data);

}  // namespace mob_survivor::net
