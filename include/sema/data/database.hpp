#pragma once

#include "game_data.hpp"

#include <filesystem>
#include <string>

namespace sema::data {

struct database_config {
    std::string host{"127.0.0.1"};
    unsigned short port{3306};
    std::string user;
    std::string password;
    std::string database;
    std::filesystem::path client_library{"libmysql.dll"};
};

[[nodiscard]] database_config load_database_config(const std::filesystem::path& path);
[[nodiscard]] game_data_store load_game_data_mysql(
    const database_config& config,
    const std::filesystem::path& walk_mesh_directory = {});

} // namespace sema::data
