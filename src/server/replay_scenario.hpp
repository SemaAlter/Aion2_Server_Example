#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct replay_event {
    std::chrono::milliseconds delay{};
    std::vector<unsigned char> body;
};

struct replay_scenario {
    std::string name;
    std::uint32_t player_entity_id;
    std::uint32_t map_id;
    std::vector<replay_event> events;
};

struct replay_phase {
    std::string name;
    std::uint32_t source_player_entity_id;
    std::uint64_t captured_server_time_milliseconds;
    std::vector<replay_event> events;
};

[[nodiscard]] replay_scenario load_replay_scenario(const std::filesystem::path& path);
[[nodiscard]] replay_phase load_replay_phase(const std::filesystem::path& path);
[[nodiscard]] std::vector<replay_event> prepare_replay_phase(
    const replay_phase& phase,
    std::uint32_t player_entity_id,
    std::uint64_t server_time_milliseconds);
