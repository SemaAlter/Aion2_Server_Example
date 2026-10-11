#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sema {

enum class lobby_client_message : std::uint16_t {
    hello = 0x3900,
};

enum class lobby_server_message : std::uint16_t {
    hello = 0x3901,
    clock_sync = 0x3903,
    account_session = 0x3906,
    server_list = 0x3909,
    character_summary = 0x390B,
    join_accepted = 0x390D,
    gameplay_redirect = 0x390F,
};

enum class lobby_faction : std::uint8_t {
    elyos = 1,
    asmodians = 2,
};

enum class lobby_availability : std::uint8_t {
    unavailable = 0,
    available = 1,
};

enum class lobby_status : std::uint8_t {
    none = 0,
    new_server = 0x01,
    recommended = 0x02,
    character_creation_restricted = 0x04,
};

constexpr lobby_status operator|(const lobby_status left, const lobby_status right) {
    return static_cast<lobby_status>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

struct lobby_server {
    std::uint16_t id = 4095;
    std::string name = "Sema Local";
    lobby_faction faction = lobby_faction::elyos;
    lobby_availability availability = lobby_availability::available;
    lobby_status status = lobby_status::new_server | lobby_status::recommended;
    std::uint8_t load_percent = 10;
};

[[nodiscard]] std::vector<std::uint8_t> encode_lobby_server_list(std::span<const lobby_server> servers);
[[nodiscard]] std::vector<std::uint8_t> encode_lobby_account(std::uint16_t server_id);
[[nodiscard]] std::vector<std::uint8_t> encode_empty_lobby_characters();
[[nodiscard]] std::vector<std::uint8_t> encode_lobby_join_accepted(std::uint16_t server_id);
[[nodiscard]] std::vector<std::uint8_t> encode_lobby_redirect(
    std::uint16_t server_id,
    const std::string& host,
    std::uint16_t port);

} // namespace sema
