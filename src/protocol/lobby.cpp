#include "sema/lobby.hpp"

#include <stdexcept>

namespace {

void append_u16(std::vector<std::uint8_t>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

void append_name(std::vector<std::uint8_t>& bytes, const std::string& value) {
    if (value.empty() || value.size() > 255) throw std::invalid_argument("lobby server name must contain 1 to 255 bytes");
    bytes.push_back(static_cast<std::uint8_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

} // namespace

namespace sema {

std::vector<std::uint8_t> encode_lobby_server_list(const std::span<const lobby_server> servers) {
    if (servers.empty() || servers.size() > 255) {
        throw std::invalid_argument("lobby server list must contain 1 to 255 entries");
    }

    std::vector<std::uint8_t> body{0x09, 0x39, 0x00, 0x00, static_cast<std::uint8_t>(servers.size())};
    for (std::size_t i = 0; i < servers.size(); ++i) {
        const auto& server = servers[i];
        if (server.id == 0) throw std::invalid_argument("lobby server ID cannot be zero");
        if (server.load_percent > 100) throw std::invalid_argument("lobby load percent cannot exceed 100");

        append_u16(body, server.id);
        body.push_back(static_cast<std::uint8_t>(server.faction));
        append_u16(body, server.id);
        append_u16(body, 0);
        append_name(body, server.name);

        if (i % 4 == 0) {
            std::uint8_t availability_mask = 0;
            for (std::size_t offset = 0; offset < 4 && i + offset < servers.size(); ++offset) {
                availability_mask |= static_cast<std::uint8_t>(servers[i + offset].availability) << (2 * offset);
            }
            body.push_back(availability_mask);
        }

        body.push_back(static_cast<std::uint8_t>(server.status));
        body.push_back(server.load_percent);
        body.insert(body.end(), {0x50, 0x01, 0x00}); // Constant in every observed current Europe record.
    }
    body.insert(body.end(), {0x01, 0x01});
    return body;
}

std::vector<std::uint8_t> encode_lobby_account(const std::uint16_t server_id) {
    if (server_id == 0) throw std::invalid_argument("lobby account requires a server ID");
    std::vector<std::uint8_t> body{0x06, 0x39, 0x00, 0x00, 0x00};
    append_name(body, "00000000-0000-0000-0000-000000000001");
    body.insert(body.end(), {0x01, 0x00, 0x00});
    append_name(body, "1:00000000-0000-0000-0000-000000000002");
    append_u16(body, server_id);
    body.insert(body.end(), {0x01, 0x00, 0x00, 0x00, 0x00});
    append_u16(body, server_id);
    body.insert(body.end(), {0x03, 0x09, 0x03});
    return body;
}

std::vector<std::uint8_t> encode_empty_lobby_characters() {
    return {0x0B, 0x39, 0x00, 0x00, 0x00, 0x01, 0x01};
}

std::vector<std::uint8_t> encode_lobby_join_accepted(const std::uint16_t server_id) {
    if (server_id == 0) throw std::invalid_argument("lobby join acceptance requires a server ID");
    std::vector<std::uint8_t> body{0x0D, 0x39, 0x00, 0x00, 0x00};
    append_u16(body, server_id);
    return body;
}

std::vector<std::uint8_t> encode_lobby_redirect(
    const std::uint16_t server_id,
    const std::string& host,
    const std::uint16_t port) {
    if (server_id == 0 || port == 0) throw std::invalid_argument("lobby redirect requires a server ID and port");
    std::vector<std::uint8_t> body{0x0F, 0x39, 0x00, 0x00};
    append_u16(body, server_id);
    append_name(body, host);
    append_u16(body, port);
    return body;
}

} // namespace sema
