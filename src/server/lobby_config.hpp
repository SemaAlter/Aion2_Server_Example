#pragma once

#include "sema/lobby.hpp"

#include <filesystem>

[[nodiscard]] sema::lobby_server load_lobby_server_config(const std::filesystem::path& path);
