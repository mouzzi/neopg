// OpenPGP key expiration time subpacket (implementation)
// Copyright 2018 The NeoPG developers
//
// NeoPG is released under the Simplified BSD License (see license.txt)

#include <neopg/openpgp/signature/subpacket/key_expiration_time_subpacket.h>

#include <neopg/parser/parser_input.h>
#include <neopg/utils/stream.h>

#include <neopg/intern/cplusplus.h>
#include <neopg/intern/pegtl.h>

using namespace NeoPG;

namespace NeoPG {
namespace key_expiration_time_subpacket {

using namespace pegtl;

// Grammar
struct expiration : bytes<4> {};
struct grammar : must<expiration, eof> {};

// Action
template <typename Rule>
struct action : nothing<Rule> {};

template <>
struct action<expiration> : bind<KeyExpirationTimeSubpacket, uint32_t,
                                 &KeyExpirationTimeSubpacket::m_expiration> {};

// Control
template <typename Rule>
struct control : pegtl::normal<Rule> {
  static const std::string error_message;

  template <typename Input, typename... States>
  static void raise(const Input& in, States&&...) {
    throw parser_error(error_message, in);
  }
};

template <>
const std::string control<expiration>::error_message =
    "key expiration time subpacket is invalid";

template <>
const std::string control<eof>::error_message =
    "key expiration time subpacket is too large";

}  // namespace key_expiration_time_subpacket
}  // namespace NeoPG

std::unique_ptr<KeyExpirationTimeSubpacket>
KeyExpirationTimeSubpacket::create_or_throw(ParserInput& in) {
  auto packet = NeoPG::make_unique<KeyExpirationTimeSubpacket>();
  pegtl::parse<key_expiration_time_subpacket::grammar,
               key_expiration_time_subpacket::action,
               key_expiration_time_subpacket::control>(in.m_impl->m_input,
                                                       *packet.get());
  return packet;
}

void KeyExpirationTimeSubpacket::write_body(std::ostream& out) const {
  out << static_cast<uint8_t>(m_expiration >> 24)
      << static_cast<uint8_t>(m_expiration >> 16)
      << static_cast<uint8_t>(m_expiration >> 8)
      << static_cast<uint8_t>(m_expiration);
}
