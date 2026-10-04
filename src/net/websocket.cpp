#include "net/websocket.hpp"

#include <string>

#include "net/base64.hpp"
#include "net/sha1.hpp"

namespace mob_survivor::net {

std::string websocket_accept(std::string_view key) {
  std::string input(key);
  input += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  const auto digest = sha1(input);
  return base64_encode(std::string_view(reinterpret_cast<const char*>(digest.data()), digest.size()));
}

bool is_websocket_upgrade(const HttpRequest& request) {
  if (request.method != "GET") return false;
  const auto upgrade = request.header("upgrade");
  const auto connection = request.header("connection");
  const auto version = request.header("sec-websocket-version");
  const auto key = request.header("sec-websocket-key");
  return upgrade && header_has_token(*upgrade, "websocket") && connection && header_has_token(*connection, "upgrade") &&
         version && *version == "13" && key && !key->empty();
}

std::string websocket_handshake_response(std::string_view key) {
  std::string out = "HTTP/1.1 101 Switching Protocols\r\n";
  out += "Upgrade: websocket\r\n";
  out += "Connection: Upgrade\r\n";
  out += "Sec-WebSocket-Accept: " + websocket_accept(key) + "\r\n\r\n";
  return out;
}

std::string encode_frame(Opcode opcode, std::string_view payload, std::optional<std::array<std::uint8_t, 4>> mask) {
  std::string out;
  out.reserve(payload.size() + 14);
  out.push_back(static_cast<char>(0x80U | static_cast<std::uint8_t>(opcode)));
  const std::uint8_t mask_bit = mask ? 0x80U : 0x00U;
  const std::size_t length = payload.size();
  if (length < 126U) {
    out.push_back(static_cast<char>(mask_bit | static_cast<std::uint8_t>(length)));
  } else if (length <= 0xFFFFU) {
    out.push_back(static_cast<char>(mask_bit | 126U));
    out.push_back(static_cast<char>((length >> 8U) & 0xFFU));
    out.push_back(static_cast<char>(length & 0xFFU));
  } else {
    out.push_back(static_cast<char>(mask_bit | 127U));
    for (int shift = 56; shift >= 0; shift -= 8) {
      out.push_back(static_cast<char>((static_cast<std::uint64_t>(length) >> static_cast<unsigned>(shift)) & 0xFFU));
    }
  }
  if (!mask) {
    out.append(payload);
    return out;
  }
  for (const std::uint8_t byte : *mask) out.push_back(static_cast<char>(byte));
  for (std::size_t i = 0; i < length; ++i) {
    out.push_back(static_cast<char>(static_cast<std::uint8_t>(payload[i]) ^ (*mask)[i % 4U]));
  }
  return out;
}

FrameDecoder::Result FrameDecoder::next(std::string& buffer, WsMessage& message) {
  while (true) {
    if (buffer.size() < 2) return Result::NeedMore;
    const auto b0 = static_cast<std::uint8_t>(buffer[0]);
    const auto b1 = static_cast<std::uint8_t>(buffer[1]);
    const bool fin = (b0 & 0x80U) != 0;
    if ((b0 & 0x70U) != 0) return Result::Error;  // no extensions were negotiated
    const auto opcode = static_cast<Opcode>(b0 & 0x0FU);
    const bool masked = (b1 & 0x80U) != 0;
    if (require_mask_ && !masked) return Result::Error;
    std::uint64_t length = b1 & 0x7FU;
    std::size_t at = 2;
    if (length == 126U) {
      if (buffer.size() < 4) return Result::NeedMore;
      length = (static_cast<std::uint64_t>(static_cast<std::uint8_t>(buffer[2])) << 8U) |
               static_cast<std::uint8_t>(buffer[3]);
      at = 4;
    } else if (length == 127U) {
      if (buffer.size() < 10) return Result::NeedMore;
      length = 0;
      for (std::size_t i = 2; i < 10; ++i) length = (length << 8U) | static_cast<std::uint8_t>(buffer[i]);
      at = 10;
    }
    const bool control = (static_cast<std::uint8_t>(opcode) & 0x08U) != 0;
    if (control && (!fin || length > 125U)) return Result::Error;
    if (length > max_message_ || (!control && fragment_.size() + length > max_message_)) return Result::Error;
    std::array<std::uint8_t, 4> mask{};
    if (masked) {
      if (buffer.size() < at + 4) return Result::NeedMore;
      for (std::size_t i = 0; i < 4; ++i) mask[i] = static_cast<std::uint8_t>(buffer[at + i]);
      at += 4;
    }
    if (buffer.size() < at + length) return Result::NeedMore;

    std::string payload = buffer.substr(at, static_cast<std::size_t>(length));
    if (masked) {
      for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>(static_cast<std::uint8_t>(payload[i]) ^ mask[i % 4U]);
      }
    }
    buffer.erase(0, at + static_cast<std::size_t>(length));

    switch (opcode) {
      case Opcode::Close:
      case Opcode::Ping:
      case Opcode::Pong:
        message.opcode = opcode;
        message.payload = std::move(payload);
        return Result::Message;
      case Opcode::Text:
      case Opcode::Binary:
        if (in_fragment_) return Result::Error;
        if (fin) {
          message.opcode = opcode;
          message.payload = std::move(payload);
          return Result::Message;
        }
        in_fragment_ = true;
        fragment_opcode_ = opcode;
        fragment_ = std::move(payload);
        continue;
      case Opcode::Continuation:
        if (!in_fragment_) return Result::Error;
        fragment_ += payload;
        if (!fin) continue;
        in_fragment_ = false;
        message.opcode = fragment_opcode_;
        message.payload = std::move(fragment_);
        fragment_.clear();
        return Result::Message;
    }
    return Result::Error;  // reserved opcode
  }
}

}  // namespace mob_survivor::net
