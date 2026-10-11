#include "lobby_session.hpp"

#include "sema/handshake.hpp"

#include <stdexcept>
#include <utility>

lobby_session::lobby_session(lobby_session_config config) : config_(std::move(config)) {
    if (config_.session_data.empty()) throw std::invalid_argument("lobby session data cannot be empty");
}

std::vector<std::uint8_t> lobby_session::respond(const std::span<const std::uint8_t> client_body) {
    ++sequence_;
    switch (sequence_) {
    case 1: {
        const auto hello = sema::parse_lobby_client_hello(client_body);
        return sema::create_lobby_server_hello(hello, config_.session_data);
    }
    case 2:
        return sema::encode_lobby_account(config_.server.id);
    case 3:
        return sema::encode_empty_lobby_characters();
    case 4:
        return sema::encode_lobby_server_list(std::span(&config_.server, 1));
    case 5:
        return sema::encode_lobby_join_accepted(config_.server.id);
    case 6:
        return sema::encode_lobby_redirect(config_.server.id, config_.gameplay_host, config_.gameplay_port);
    default:
        return {};
    }
}
