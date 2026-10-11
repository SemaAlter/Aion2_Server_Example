#include "lobby_config.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

std::string trim(std::string value) {
    const auto is_space = [](const unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), is_space));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), is_space).base(), value.end());
    return value;
}

unsigned parse_unsigned(const std::string& value, const std::string& key, const unsigned maximum) {
    unsigned result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || result > maximum) {
        throw std::runtime_error("invalid " + key + " value: " + value);
    }
    return result;
}

bool parse_bool(std::string value, const std::string& key) {
    std::ranges::transform(value, value.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "true" || value == "yes" || value == "1") return true;
    if (value == "false" || value == "no" || value == "0") return false;
    throw std::runtime_error("invalid " + key + " value: " + value);
}

} // namespace

sema::lobby_server load_lobby_server_config(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("could not open lobby config: " + path.string());

    sema::lobby_server result;
    bool is_new = false;
    bool is_recommended = false;
    bool creation_restricted = false;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line.starts_with('#') || line.starts_with(';')) continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error("lobby config line " + std::to_string(line_number) + " requires key=value");
        }
        auto key = trim(line.substr(0, equals));
        auto value = trim(line.substr(equals + 1));
        std::ranges::transform(key, key.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (key == "server_id") {
            result.id = static_cast<std::uint16_t>(parse_unsigned(value, key, 65535));
            if (result.id == 0) throw std::runtime_error("server_id cannot be zero");
        } else if (key == "name") {
            if (value.empty() || value.size() > 255) throw std::runtime_error("name must contain 1 to 255 bytes");
            result.name = value;
        } else if (key == "faction") {
            std::ranges::transform(value, value.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (value == "elyos") result.faction = sema::lobby_faction::elyos;
            else if (value == "asmodians") result.faction = sema::lobby_faction::asmodians;
            else throw std::runtime_error("faction must be elyos or asmodians");
        } else if (key == "available") {
            result.availability = parse_bool(value, key)
                ? sema::lobby_availability::available
                : sema::lobby_availability::unavailable;
        } else if (key == "new") {
            is_new = parse_bool(value, key);
        } else if (key == "recommended") {
            is_recommended = parse_bool(value, key);
        } else if (key == "character_creation_restricted") {
            creation_restricted = parse_bool(value, key);
        } else if (key == "load_percent") {
            result.load_percent = static_cast<std::uint8_t>(parse_unsigned(value, key, 100));
        } else {
            throw std::runtime_error("unknown lobby config key on line " + std::to_string(line_number) + ": " + key);
        }
    }

    result.status = sema::lobby_status::none;
    if (is_new) result.status = result.status | sema::lobby_status::new_server;
    if (is_recommended) result.status = result.status | sema::lobby_status::recommended;
    if (creation_restricted) result.status = result.status | sema::lobby_status::character_creation_restricted;
    return result;
}
