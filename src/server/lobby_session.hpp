#pragma once

#include "sema/lobby.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

struct lobby_session_config {
    sema::lobby_server server;
    std::string gameplay_host;
    std::uint16_t gameplay_port = 13328;
    std::vector<std::uint8_t> session_data;
};

class lobby_session {
public:
    explicit lobby_session(lobby_session_config config);
    [[nodiscard]] std::vector<std::uint8_t> respond(std::span<const std::uint8_t> client_body);

private:
    lobby_session_config config_;
    std::size_t sequence_ = 0;
};
