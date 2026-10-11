#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace sema {

struct client_hello {
    std::vector<std::uint8_t> public_key_der;
    std::vector<std::uint8_t> trailer;
};

struct server_hello {
    std::vector<std::uint8_t> header;
    std::vector<std::uint8_t> encrypted_session_data;
    std::vector<std::uint8_t> trailer;
};

[[nodiscard]] client_hello parse_client_hello(std::span<const std::uint8_t> body);
[[nodiscard]] client_hello parse_lobby_client_hello(std::span<const std::uint8_t> body);
[[nodiscard]] server_hello parse_server_hello(std::span<const std::uint8_t> body);
[[nodiscard]] std::vector<std::uint8_t> create_server_hello(
    const client_hello& hello,
    std::span<const std::uint8_t> session_data);
[[nodiscard]] std::vector<std::uint8_t> create_lobby_server_hello(
    const client_hello& hello,
    std::span<const std::uint8_t> session_data);

} // namespace sema
