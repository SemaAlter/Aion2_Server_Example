#include "replay_scenario.hpp"

#include "sema/gameplay.hpp"

#include <fstream>
#include <limits>
#include <regex>
#include <stdexcept>

namespace {

std::vector<unsigned char> decode_hex(const std::string& text) {
    if (text.empty() || text.size() % 2 != 0) throw std::runtime_error("scenario bodyHex must contain complete bytes");
    std::vector<unsigned char> result;
    result.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        try {
            result.push_back(static_cast<unsigned char>(std::stoul(text.substr(i, 2), nullptr, 16)));
        } catch (...) {
            throw std::runtime_error("scenario bodyHex contains invalid hexadecimal data");
        }
    }
    return result;
}

std::uint64_t required_unsigned(const std::string& json, const char* field) {
    const std::regex pattern(std::string("\"") + field + R"json("\s*:\s*([0-9]+))json");
    std::smatch match;
    if (!std::regex_search(json, match, pattern)) {
        throw std::runtime_error(std::string("replay data requires ") + field);
    }
    return std::stoull(match[1].str());
}

std::vector<replay_event> parse_events(const std::string& json) {
    const std::regex event_pattern(
        R"json(\{\s*"delayMilliseconds"\s*:\s*(-?[0-9]+)\s*,\s*"bodyHex"\s*:\s*"([0-9A-Fa-f]+)"\s*\})json");
    std::vector<replay_event> result;
    for (auto it = std::sregex_iterator(json.begin(), json.end(), event_pattern); it != std::sregex_iterator(); ++it) {
        const auto delay = std::stoll((*it)[1].str());
        if (delay < 0) throw std::runtime_error("replay delay cannot be negative");
        result.push_back({std::chrono::milliseconds(delay), decode_hex((*it)[2].str())});
    }
    if (result.empty()) throw std::runtime_error("replay data requires at least one valid event");
    return result;
}

void append_uvarint(std::vector<unsigned char>& output, std::uint32_t value) {
    do {
        auto byte = static_cast<unsigned char>(value & 0x7FU);
        value >>= 7U;
        if (value != 0) byte |= 0x80U;
        output.push_back(byte);
    } while (value != 0);
}

std::uint64_t read_u64(const std::vector<unsigned char>& bytes, const std::size_t offset) {
    std::uint64_t result = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        result |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
    }
    return result;
}

void write_u64(std::vector<unsigned char>& bytes, const std::size_t offset, const std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[offset + index] = static_cast<unsigned char>((value >> (index * 8U)) & 0xFFU);
    }
}

void replace_all(
    std::vector<unsigned char>& body,
    const std::vector<unsigned char>& source,
    const std::vector<unsigned char>& replacement) {
    if (source.size() != replacement.size()) {
        throw std::runtime_error("replay player IDs must use equal-width varints");
    }
    for (std::size_t offset = 0; offset + source.size() <= body.size(); ++offset) {
        if (std::equal(source.begin(), source.end(), body.begin() + offset)) {
            std::copy(replacement.begin(), replacement.end(), body.begin() + offset);
            offset += source.size() - 1;
        }
    }
}

} // namespace

replay_scenario load_replay_scenario(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open replay scenario: " + path.string());
    const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

    const std::regex name_pattern(R"json("name"\s*:\s*"([^"]+)")json");
    std::smatch name_match;
    if (!std::regex_search(json, name_match, name_pattern)) throw std::runtime_error("scenario requires a name");

    const std::regex player_pattern(R"json("playerEntityId"\s*:\s*([0-9]+))json");
    std::smatch player_match;
    if (!std::regex_search(json, player_match, player_pattern)) {
        throw std::runtime_error("scenario requires playerEntityId");
    }
    const auto player_entity_id = std::stoul(player_match[1].str());
    if (player_entity_id == 0) throw std::runtime_error("scenario playerEntityId cannot be zero");

    const std::regex map_pattern(R"json("mapId"\s*:\s*([0-9]+))json");
    std::smatch map_match;
    if (!std::regex_search(json, map_match, map_pattern)) throw std::runtime_error("scenario requires mapId");
    const auto map_id = std::stoul(map_match[1].str());
    if (map_id == 0) throw std::runtime_error("scenario mapId cannot be zero");

    replay_scenario result{name_match[1].str(), static_cast<std::uint32_t>(player_entity_id),
        static_cast<std::uint32_t>(map_id), parse_events(json)};
    return result;
}

replay_phase load_replay_phase(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open replay phase: " + path.string());
    const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::regex name_pattern(R"json("name"\s*:\s*"([^"]+)")json");
    std::smatch name_match;
    if (!std::regex_search(json, name_match, name_pattern)) throw std::runtime_error("replay phase requires a name");
    const auto source_player = required_unsigned(json, "sourcePlayerEntityId");
    const auto captured_time = required_unsigned(json, "capturedServerTimeMilliseconds");
    if (source_player == 0 || source_player > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("replay phase sourcePlayerEntityId is invalid");
    }
    // FF FF containers are LZ4-compressed; replace them with their records so player IDs,
    // timestamps, and character records can be adapted to the live session.
    std::vector<replay_event> events;
    for (auto& event : parse_events(json)) {
        if (event.body.size() < 6 || event.body[0] != 0xFF || event.body[1] != 0xFF) {
            events.push_back(std::move(event));
            continue;
        }
        auto delay = event.delay;
        for (auto& record : sema::unpack_gameplay_snapshot(event.body)) {
            events.push_back({delay, std::move(record)});
            delay = std::chrono::milliseconds(0);
        }
    }
    return {name_match[1].str(), static_cast<std::uint32_t>(source_player), captured_time, std::move(events)};
}

std::vector<replay_event> prepare_replay_phase(
    const replay_phase& phase,
    const std::uint32_t player_entity_id,
    const std::uint64_t server_time_milliseconds) {
    std::vector<unsigned char> source_player;
    std::vector<unsigned char> target_player;
    append_uvarint(source_player, phase.source_player_entity_id);
    append_uvarint(target_player, player_entity_id);

    auto result = phase.events;
    constexpr std::uint64_t timestamp_window = 30'000;
    for (auto& event : result) {
        replace_all(event.body, source_player, target_player);
        for (std::size_t offset = 0; offset + 8 <= event.body.size(); ++offset) {
            const auto captured = read_u64(event.body, offset);
            const auto lower = phase.captured_server_time_milliseconds - timestamp_window;
            const auto upper = phase.captured_server_time_milliseconds + timestamp_window;
            if (captured < lower || captured > upper) continue;
            const auto delta = static_cast<std::int64_t>(captured)
                - static_cast<std::int64_t>(phase.captured_server_time_milliseconds);
            write_u64(event.body, offset, static_cast<std::uint64_t>(
                static_cast<std::int64_t>(server_time_milliseconds) + delta));
            offset += 7;
        }
    }
    return result;
}
