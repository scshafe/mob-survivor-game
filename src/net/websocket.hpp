#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "net/http.hpp"

namespace mob_survivor::net {

// RFC 6455: the value of Sec-WebSocket-Accept for a Sec-WebSocket-Key.
[[nodiscard]] std::string websocket_accept(std::string_view key);

// A GET with Upgrade: websocket, Connection: upgrade, version 13 and a key.
[[nodiscard]] bool is_websocket_upgrade(const HttpRequest& request);

[[nodiscard]] std::string websocket_handshake_response(std::string_view key);

enum class Opcode : std::uint8_t {
  Continuation = 0x0,
  Text = 0x1,
  Binary = 0x2,
  Close = 0x8,
  Ping = 0x9,
  Pong = 0xA,
};

// One whole frame (FIN set). Servers send unmasked frames; a mask is only
// for clients (and tests that play one).
[[nodiscard]] std::string encode_frame(Opcode opcode, std::string_view payload,
                                       std::optional<std::array<std::uint8_t, 4>> mask = std::nullopt);

struct WsMessage {
  Opcode opcode = Opcode::Text;
  std::string payload;
};

// Turns the bytes a client sends into whole messages: it unmasks, joins
// fragments and lets control frames through between them.
class FrameDecoder {
 public:
  enum class Result { NeedMore, Message, Error };

  explicit FrameDecoder(std::size_t max_message = 64 * 1024, bool require_mask = true)
      : max_message_(max_message), require_mask_(require_mask) {}

  // Takes at most one message off the front of `buffer` (consuming its bytes).
  Result next(std::string& buffer, WsMessage& message);

 private:
  std::size_t max_message_;
  bool require_mask_;
  bool in_fragment_ = false;
  Opcode fragment_opcode_ = Opcode::Text;
  std::string fragment_;
};

}  // namespace mob_survivor::net
