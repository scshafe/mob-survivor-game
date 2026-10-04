#pragma once

#include <string>
#include <string_view>

namespace mob_survivor::net {

// Standard base64 with padding (RFC 4648 section 4).
[[nodiscard]] std::string base64_encode(std::string_view bytes);

}  // namespace mob_survivor::net
