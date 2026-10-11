#include "sema/gameplay.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace {

std::string read_name(const std::span<const std::uint8_t> bytes, std::size_t& offset) {
    if (offset >= bytes.size()) throw std::runtime_error("gameplay login name length is missing");
    const auto length = bytes[offset++];
    if (offset + length > bytes.size()) throw std::runtime_error("gameplay login name is truncated");
    std::string result(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
    offset += length;
    return result;
}

void append_u16(std::vector<std::uint8_t>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

void append_u32(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    append_u16(bytes, static_cast<std::uint16_t>(value));
    append_u16(bytes, static_cast<std::uint16_t>(value >> 16));
}

void write_u32(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value) {
    if (offset + 4 > bytes.size()) throw std::runtime_error("gameplay integer write is out of range");
    for (std::size_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

void write_u64(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint64_t value) {
    write_u32(bytes, offset, static_cast<std::uint32_t>(value));
    write_u32(bytes, offset + 4, static_cast<std::uint32_t>(value >> 32));
}

void append_u64(std::vector<std::uint8_t>& bytes, const std::uint64_t value) {
    append_u32(bytes, static_cast<std::uint32_t>(value));
    append_u32(bytes, static_cast<std::uint32_t>(value >> 32));
}

void append_float(std::vector<std::uint8_t>& bytes, const float value) {
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

std::uint64_t read_u64(const std::span<const std::uint8_t> bytes, const std::size_t offset) {
    if (offset + 8 > bytes.size()) throw std::runtime_error("gameplay timestamp is truncated");
    std::uint64_t result = 0;
    for (std::size_t i = 0; i < 8; ++i) result |= static_cast<std::uint64_t>(bytes[offset + i]) << (i * 8);
    return result;
}

std::uint32_t read_u32(const std::span<const std::uint8_t> bytes, const std::size_t offset) {
    if (offset + 4 > bytes.size()) throw std::runtime_error("gameplay integer is truncated");
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint32_t read_uvarint(const std::span<const std::uint8_t> bytes, std::size_t& offset) {
    std::uint32_t result = 0;
    for (unsigned shift = 0; shift < 35 && offset < bytes.size(); shift += 7) {
        const auto byte = bytes[offset++];
        result |= static_cast<std::uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) return result;
    }
    throw std::runtime_error("gameplay varint is invalid");
}

void append_uvarint(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    do {
        auto byte = static_cast<std::uint8_t>(value & 0x7F);
        value >>= 7;
        if (value != 0) byte |= 0x80;
        bytes.push_back(byte);
    } while (value != 0);
}

void append_bytes(std::vector<std::uint8_t>& bytes, std::initializer_list<std::uint8_t> values) {
    bytes.insert(bytes.end(), values);
}

void append_name(std::vector<std::uint8_t>& bytes, const std::string& value) {
    if (value.empty() || value.size() > 255) throw std::invalid_argument("gameplay login name must contain 1 to 255 bytes");
    bytes.push_back(static_cast<std::uint8_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

} // namespace

namespace sema {

bool has_gameplay_opcode(
    const std::span<const std::uint8_t> body,
    const gameplay_client_message message) noexcept {
    if (body.size() < 2) return false;
    const auto opcode = static_cast<std::uint16_t>(body[0])
        | (static_cast<std::uint16_t>(body[1]) << 8);
    return opcode == static_cast<std::uint16_t>(message);
}

bool has_gameplay_opcode(
    const std::span<const std::uint8_t> body,
    const gameplay_server_message message) noexcept {
    if (body.size() < 2) return false;
    const auto opcode = static_cast<std::uint16_t>(body[0])
        | (static_cast<std::uint16_t>(body[1]) << 8);
    return opcode == static_cast<std::uint16_t>(message);
}

gameplay_client_login parse_gameplay_client_login(const std::span<const std::uint8_t> body) {
    if (!has_gameplay_opcode(body, gameplay_client_message::login)) {
        throw std::runtime_error("frame is not a gameplay client login");
    }
    std::size_t offset = 2;
    gameplay_client_login result;
    result.account_session = read_name(body, offset);
    if (offset >= body.size() || body[offset++] != 0) throw std::runtime_error("gameplay client login separator is invalid");
    result.lobby_token = read_name(body, offset);
    if (body.size() - offset != 10) throw std::runtime_error("gameplay client login trailer has an unexpected size");
    return result;
}

std::vector<std::uint8_t> encode_empty_gameplay_login(
    const gameplay_client_login& client,
    const std::uint16_t server_id,
    const std::uint64_t login_time_milliseconds,
    const std::span<const std::vector<std::uint8_t>> characters) {
    if (server_id == 0) throw std::invalid_argument("gameplay login requires a server ID");
    if (characters.size() > 255) throw std::invalid_argument("gameplay login has too many characters");
    constexpr std::uint64_t eight_hours_milliseconds = 8ULL * 60 * 60 * 1000;

    std::vector<std::uint8_t> body{0x15, 0x36, 0x00, 0x00};
    append_u64(body, login_time_milliseconds);
    append_u64(body, login_time_milliseconds + eight_hours_milliseconds);
    body.push_back(static_cast<std::uint8_t>(characters.size()));
    for (const auto& character : characters) body.insert(body.end(), character.begin(), character.end());
    if (characters.empty()) append_u32(body, 0); // observed empty-account form
    else body.push_back(0);                       // official-tutorial-start 2.18 s
    append_name(body, client.account_session);
    append_name(body, std::to_string(server_id));
    append_name(body, "5301"); // Stable region/build identifier in both captured logins.
    body.insert(body.end(), {0x01, 0x1E, 0x00});
    return body;
}

gameplay_ping parse_gameplay_ping(const std::span<const std::uint8_t> body) {
    // Bytes after the echoed clock are client-state data. The logged-out form
    // has one zero byte; entering the tutorial flow expands it to five bytes.
    if (body.size() < 10 || !has_gameplay_opcode(body, gameplay_client_message::ping)) {
        throw std::runtime_error("frame is not a gameplay ping");
    }
    return {read_u64(body, 2)};
}

gameplay_compressed_json parse_gameplay_minimap_filter_settings(
    const std::span<const std::uint8_t> body) {
    if (!has_gameplay_opcode(body, gameplay_client_message::minimap_filter_settings)) {
        throw std::runtime_error("frame is not gameplay minimap filter settings");
    }

    std::size_t offset = 2;
    const auto declared_compressed_length = read_uvarint(body, offset);
    if (declared_compressed_length < 4 || offset + 4 > body.size()) {
        throw std::runtime_error("gameplay minimap filter settings header is invalid");
    }
    const auto uncompressed_length = read_u32(body, offset);
    offset += 4;
    const auto compressed_length = static_cast<std::size_t>(declared_compressed_length - 4);
    if (body.size() - offset != compressed_length) {
        throw std::runtime_error("gameplay minimap filter settings compressed size is invalid");
    }
    return {
        declared_compressed_length,
        uncompressed_length,
        std::vector<std::uint8_t>(body.begin() + static_cast<std::ptrdiff_t>(offset), body.end()),
    };
}

gameplay_player_name parse_gameplay_player_name(const std::span<const std::uint8_t> body) {
    if (!has_gameplay_opcode(body, gameplay_server_message::player_name) || body.size() < 6
        || body[2] != 0 || body[3] != 0) {
        throw std::runtime_error("frame is not a gameplay player-name message");
    }
    std::size_t offset = 4;
    auto name = read_name(body, offset);
    if (body.size() - offset != 1) {
        throw std::runtime_error("gameplay player-name trailer has an unexpected size");
    }
    return {std::move(name), body[offset]};
}

gameplay_movement parse_gameplay_movement(const std::span<const std::uint8_t> body) {
    // 00 37 / 01 37 / 0F 37: mode byte then XYZ; 18 37 snapshot: two bytes then XYZ.
    const bool snapshot = has_gameplay_opcode(body, gameplay_client_message::movement_snapshot);
    const bool supported = snapshot || has_gameplay_opcode(body, gameplay_client_message::movement_started)
        || has_gameplay_opcode(body, gameplay_client_message::movement_updated)
        || has_gameplay_opcode(body, gameplay_client_message::dash_path_update);
    const std::size_t offset = snapshot ? 4 : 3;
    if (!supported || body.size() < offset + 12) {
        throw std::runtime_error("gameplay movement is truncated or unsupported");
    }
    return {
        std::bit_cast<float>(read_u32(body, offset)),
        std::bit_cast<float>(read_u32(body, offset + 4)),
        std::bit_cast<float>(read_u32(body, offset + 8)),
    };
}

std::vector<std::uint8_t> encode_gameplay_ping(
    const gameplay_ping client,
    const std::uint64_t server_time_milliseconds) {
    std::vector<std::uint8_t> body{0x03, 0x36};
    append_u64(body, client.client_time_milliseconds);
    append_u64(body, server_time_milliseconds);
    return body;
}

std::vector<std::uint8_t> encode_gameplay_tick(const std::uint64_t server_time_milliseconds) {
    std::vector<std::uint8_t> body{0x00, 0x36};
    append_u64(body, server_time_milliseconds);
    return body;
}

std::vector<std::uint8_t> encode_gameplay_exit_result() {
    return {0x31, 0x36, 0x00, 0x00};
}

bool refresh_gameplay_server_time(
    std::vector<std::uint8_t>& body,
    const std::uint64_t server_time_milliseconds) {
    if (!has_gameplay_opcode(body, gameplay_server_message::scheduled_event_state)
        || body.size() != 20 || body[2] != 1) {
        return false;
    }
    write_u64(body, 11, server_time_milliseconds);
    return true;
}

std::vector<gameplay_timed_message> gameplay_tutorial_dash_sequence(
    const gameplay_client_cast& request,
    const gameplay_skill_definition& skill,
    const std::uint32_t player_entity_id,
    const std::uint8_t action_sequence,
    const std::uint8_t player_state,
    const std::uint64_t server_time_milliseconds,
    const gameplay_movement position) {
    if (skill.kind != gameplay_skill_kind::dash || request.skill_id != skill.skill_id || player_entity_id != 1841) {
        throw std::invalid_argument("dash request does not match the loaded tutorial data");
    }
    // Captured response to the first Elyos tutorial dash. The recording's stamina values and its
    // 22 37 / 42 37 sprint end are not replayed: holding the key keeps sprinting until the
    // player stops, attacks, or interacts (official-tutorial-combat-door 60.5-64.0 s), and
    // tutorial_world owns the stamina gauge.
    std::vector<gameplay_timed_message> result{
        {0, {0x03, 0x8D, 0xB1, 0x0E, 0x00, 0x00, 0x00, 0x00}},
        {0, {0x41, 0x37, 0xB1, 0x0E, 0x00, 0x00}},
        {0, {0x02, 0x38, 0xB1, 0x0E, 0x00, 0xA2, 0x0F, 0x00, 0x00, 0x01, 0x02,
             0xB1, 0x0E, 0x46, 0xA4, 0xFC, 0xBD, 0xE2, 0xFF, 0xC7, 0x42, 0xBC, 0xF1,
             0x5A, 0xBE, 0x00, 0x00, 0x00, 0x00, 0x90, 0x4E, 0x01, 0xF4, 0x03}},
        {0, {0x01, 0x38, 0x00, 0x00, 0x00, 0xA1, 0x0F, 0x00, 0x00}},
        // The recorded constant here was one byte short of the official 43-byte record.
        {0, encode_gameplay_movement_action(player_entity_id, player_state, 200, 300, server_time_milliseconds, position)},
        {49, {0x04, 0x38, 0xB1, 0x0E, 0x00, 0x00, 0xB1, 0x0E, 0xA2, 0x0F, 0x00,
              0x00, 0x01, 0x02, 0xEF, 0x1A, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00, 0x90,
              0x4E, 0x01, 0x00}},
        {150, {0x06, 0x38, 0xB1, 0x0E, 0xA2, 0x0F, 0x00, 0x00, 0x01, 0x00}},
        {0, {0x2C, 0x38, 0xB1, 0x0E, 0x01, 0x00, 0x08, 0x07}},
    };
    // Facing and the direction vector follow the target entity. Both belong
    // to the current input; replaying either value from the capture sends the
    // client in the captured direction.
    std::copy(request.facing.begin(), request.facing.end(),
        result[2].body.begin() + 13);
    std::copy(request.position_or_direction.begin(), request.position_or_direction.end(),
        result[2].body.begin() + 17);
    write_u32(result[2].body, 5, skill.server_skill_id);
    result[2].body[9] = action_sequence;
    write_u32(result[3].body, 5, skill.skill_id);
    write_u32(result[5].body, 8, skill.server_skill_id);
    result[5].body[12] = action_sequence;
    write_u32(result[6].body, 4, skill.server_skill_id);
    result[6].body[8] = action_sequence;
    // Each dash advances the player state counter (official 08, 0A, ... 12, 14); replaying
    // the captured 08 after later states is ignored by the client.
    result[7].body[6] = player_state;
    return result;
}

gameplay_client_cast parse_gameplay_client_cast(const std::span<const std::uint8_t> body) {
    if (body.size() < 11 || !has_gameplay_opcode(body, gameplay_client_message::cast)) {
        throw std::runtime_error("frame is not a gameplay cast request");
    }
    std::size_t target_offset = 9;
    gameplay_client_cast result{
        static_cast<gameplay_cast_phase>(body[2]), read_u32(body, 4), read_uvarint(body, target_offset), {}, {}};
    if (target_offset + 16 > body.size()) throw std::runtime_error("gameplay cast position is truncated");
    std::copy_n(body.begin() + static_cast<std::ptrdiff_t>(target_offset),
        result.facing.size(), result.facing.begin());
    std::copy_n(body.begin() + static_cast<std::ptrdiff_t>(target_offset + result.facing.size()),
        result.position_or_direction.size(), result.position_or_direction.begin());
    return result;
}

std::vector<gameplay_timed_message> gameplay_tutorial_attack_sequence(
    const gameplay_client_cast request,
    const gameplay_skill_definition& skill,
    const std::uint32_t player_entity_id,
    const std::uint8_t action_sequence,
    const std::span<const gameplay_attack_target> targets) {
    if (skill.kind == gameplay_skill_kind::dash || request.skill_id != skill.skill_id || player_entity_id == 0) {
        throw std::invalid_argument("attack request does not match the loaded tutorial data");
    }
    std::vector<std::uint8_t> cast{0x02, 0x38};
    append_uvarint(cast, player_entity_id);
    cast.push_back(0);
    append_u32(cast, skill.server_skill_id);
    append_bytes(cast, {action_sequence, 0x02});
    append_uvarint(cast, request.target_entity_id);
    cast.insert(cast.end(), request.facing.begin(), request.facing.end());
    cast.insert(cast.end(), request.position_or_direction.begin(), request.position_or_direction.end());
    append_bytes(cast, {0x90, 0x4E, 0x01, 0xE8, 0x07});

    std::vector<std::uint8_t> start{0x01, 0x38, 0x00, 0x00, 0x00};
    append_u32(start, skill.server_skill_id);
    std::vector<std::uint8_t> end{0x06, 0x38};
    append_uvarint(end, player_entity_id);
    append_u32(end, skill.server_skill_id);
    append_bytes(end, {action_sequence, 0x00});

    std::vector<gameplay_timed_message> result{
        {0, {0x3B, 0x38, 0x00, 0x00}},
        {0, std::move(cast)},
    };

    if (skill.announced_next_skill_id != 0) {
        std::vector<std::uint8_t> next_combo{0x18, 0x38};
        append_u32(next_combo, skill.server_skill_id);
        next_combo.push_back(1);
        append_u32(next_combo, skill.announced_next_skill_id);
        result.push_back({0, std::move(next_combo)});
    }
    if (skill.sends_combo_stage) {
        std::vector<std::uint8_t> combo_stage{0x19, 0x38, 0x01};
        append_u32(combo_stage, skill.server_skill_id);
        result.push_back({0, std::move(combo_stage)});
    }

    result.push_back({0, std::move(start)});
    if (skill.begins_combat_state) {
        std::vector<std::uint8_t> release_state{0x21, 0x8D};
        append_uvarint(release_state, player_entity_id);
        append_bytes(release_state, {0x00, 0x01});
        result.push_back({0, std::move(release_state)});
    }
    result.push_back({100, {0x3D, 0x38, 0x00, 0x00}});

    // Official timing (official-tutorial-combat-door 99.07 s): every hit of a cast lands about
    // 0.2 s after the cast and within ~50 ms of each other, the cast completes at ~0.45 s, and
    // death records follow later; spacing hits per target made combat feel delayed.
    std::vector<gameplay_timed_message> deaths;
    std::size_t target_index = 0;
    for (const auto& target : targets) {
        std::vector<std::uint8_t> hit{0x04, 0x38};
        append_uvarint(hit, target.entity_id);
        append_bytes(hit, {0x24, 0x00});
        append_uvarint(hit, player_entity_id);
        append_u32(hit, skill.server_skill_id);
        append_bytes(hit, {action_sequence, 0x02, 0xBB, 0x0E, 0x0F, 0x00, 0x01, 0x00, 0x00, 0x00,
                           0x90, 0x4E});
        const bool first = target_index++ == 0;
        if (first) {
            append_bytes(hit, {0x08, 0x03, 0x01, 0x01, 0x01, 0x01, 0x00});
        } else {
            append_bytes(hit, {0x09, 0x02, 0x01, 0x01, 0x01, 0x00});
        }
        result.push_back({first ? 100U : 0U, std::move(hit)});
        std::vector<std::uint8_t> health{0x00, 0x8D};
        append_uvarint(health, target.entity_id);
        append_bytes(health, {0x02, 0x01, 0x00});
        append_u64(health, target.remaining_health);
        result.push_back({0, std::move(health)});
        if (target.killed) {
            std::vector<std::uint8_t> combat_state{0x21, 0x8D};
            append_uvarint(combat_state, target.entity_id);
            append_bytes(combat_state, {0x00, 0x00});
            result.push_back({0, std::move(combat_state)});
            std::vector<std::uint8_t> death{0x42, 0x36};
            append_uvarint(death, target.entity_id);
            append_bytes(death, {0x00, 0x03});
            deaths.push_back({deaths.empty() ? 1'000U : 0U, std::move(death)});
        }
    }
    result.push_back({targets.empty() ? 350U : 250U, std::move(end)});
    result.insert(result.end(), std::make_move_iterator(deaths.begin()), std::make_move_iterator(deaths.end()));
    if (skill.terminal_release_delay_milliseconds != 0) {
        std::vector<std::uint8_t> release_combo{0x21, 0x8D};
        append_uvarint(release_combo, player_entity_id);
        append_bytes(release_combo, {0x00, 0x00});
        const auto after_deaths = deaths.empty() ? 0U : 1'000U;
        result.push_back({skill.terminal_release_delay_milliseconds > after_deaths
                ? skill.terminal_release_delay_milliseconds - after_deaths : 0U,
            std::move(release_combo)});
    }
    return result;
}

std::vector<std::uint8_t> encode_gameplay_monster_spawn(
    const std::uint32_t entity_id,
    const std::uint32_t template_id,
    const float x,
    const float y,
    const float z,
    const float heading,
    const std::uint32_t max_health) {
    if (entity_id == 0 || template_id == 0 || max_health == 0) {
        throw std::invalid_argument("monster spawn is incomplete");
    }
    std::vector<std::uint8_t> body{0x41, 0x36};
    append_uvarint(body, entity_id);
    append_bytes(body, {0x0C, 0x20, 0x00});
    append_u32(body, template_id);
    append_bytes(body, {0x00, 0x02});
    append_float(body, x);
    append_float(body, y);
    append_float(body, z);
    append_float(body, heading);
    append_bytes(body, {0x00, 0x07, 0x01, 0x85, 0x01, 0x85, 0x01});
    append_u32(body, max_health);
    append_u32(body, max_health);
    body.insert(body.end(), 24, 0x00);
    body.push_back(0x01);
    body.insert(body.end(), 19, 0x00);
    append_bytes(body, {0x0B, 0x00, 0x06, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
                        0xB9, 0x28, 0x34, 0x94, 0x00});
    return body;
}

namespace {

std::uint16_t encode_heading(const float degrees) {
    auto normalized = std::fmod(degrees, 360.0F);
    if (normalized < 0.0F) normalized += 360.0F;
    return static_cast<std::uint16_t>(std::lround(normalized * 65536.0F / 360.0F));
}

} // namespace

std::vector<std::uint8_t> encode_gameplay_monster_movement_started(
    const std::uint32_t entity_id,
    const gameplay_movement position,
    const float heading) {
    if (entity_id == 0) throw std::invalid_argument("monster movement entity cannot be zero");
    std::vector<std::uint8_t> body{0x1B, 0x37};
    append_uvarint(body, entity_id);
    append_bytes(body, {0x05, 0x03});
    append_float(body, position.x);
    append_float(body, position.y);
    append_float(body, position.z);
    const auto encoded_heading = encode_heading(heading);
    append_u16(body, encoded_heading);
    append_u16(body, encoded_heading);
    return body;
}

std::vector<std::uint8_t> encode_gameplay_monster_movement_stopped(
    const std::uint32_t entity_id,
    const gameplay_movement position,
    const float heading) {
    if (entity_id == 0) throw std::invalid_argument("monster movement entity cannot be zero");
    std::vector<std::uint8_t> body{0x1A, 0x37};
    append_uvarint(body, entity_id);
    append_bytes(body, {0x00, 0x00});
    append_float(body, position.x);
    append_float(body, position.y);
    append_float(body, position.z);
    append_u16(body, encode_heading(heading));
    return body;
}

std::vector<std::uint8_t> encode_gameplay_monster_movement_updated(
    const std::uint32_t entity_id,
    const float direction_x,
    const float direction_y,
    const float heading) {
    if (entity_id == 0) throw std::invalid_argument("monster movement entity cannot be zero");
    const auto encode_direction = [](const float value) {
        return static_cast<std::uint8_t>(static_cast<std::int8_t>(
            std::clamp(std::lround(value * 53.0F), -53L, 53L)));
    };
    std::vector<std::uint8_t> body{0x1D, 0x37};
    append_uvarint(body, entity_id);
    append_bytes(body, {0x27, 0x03, encode_direction(direction_x), encode_direction(direction_y)});
    const auto encoded_heading = encode_heading(heading);
    append_u16(body, encoded_heading);
    append_u16(body, encoded_heading);
    return body;
}

std::vector<std::uint8_t> encode_gameplay_entity_resource(
    const std::uint32_t entity_id, const std::uint8_t index, const std::uint32_t value) {
    std::vector<std::uint8_t> body{0x00, 0x8D};
    append_uvarint(body, entity_id);
    append_bytes(body, {0x01, 0x01, index});
    append_u32(body, value);
    return body;
}

std::vector<std::uint8_t> encode_gameplay_social_action(
    const std::uint32_t entity_id, const std::uint32_t social_action_id, const bool begin) {
    std::vector<std::uint8_t> body{static_cast<std::uint8_t>(begin ? 0x07 : 0x08), 0x95};
    append_uvarint(body, entity_id);
    append_u32(body, social_action_id);
    append_bytes(body, {static_cast<std::uint8_t>(begin ? 0x01 : 0x02), 0x00, 0x00, 0x00, 0x00});
    return body;
}

std::vector<std::uint8_t> encode_gameplay_takeoff_height(const std::uint32_t entity_id, const float z) {
    std::vector<std::uint8_t> body{0x46, 0x37};
    append_uvarint(body, entity_id);
    append_float(body, z);
    return body;
}

std::vector<std::uint8_t> encode_gameplay_entity_health(
    const std::uint32_t entity_id,
    const std::uint64_t health) {
    std::vector<std::uint8_t> body{0x00, 0x8D};
    append_uvarint(body, entity_id);
    append_bytes(body, {0x02, 0x01, 0x00});
    append_u64(body, health);
    return body;
}

std::vector<gameplay_timed_message> gameplay_monster_spawn_sequence(
    const std::span<const std::uint8_t> spawn_body,
    const std::uint32_t entity_id,
    const std::uint32_t player_entity_id) {
    auto result = gameplay_monster_activation_sequence(entity_id, player_entity_id);
    result.insert(result.begin(), {0, {spawn_body.begin(), spawn_body.end()}});
    return result;
}

std::vector<gameplay_timed_message> gameplay_monster_activation_sequence(
    const std::uint32_t entity_id,
    const std::uint32_t player_entity_id) {
    std::vector<std::uint8_t> ownership{0x52, 0x8D};
    append_uvarint(ownership, entity_id);
    std::vector<std::uint8_t> target{0x35, 0x38};
    append_uvarint(target, entity_id);
    target.push_back(0);
    append_uvarint(target, player_entity_id);
    std::vector<std::uint8_t> combat{0x21, 0x8D};
    append_uvarint(combat, entity_id);
    append_bytes(combat, {0x00, 0x01});
    return {{25, std::move(ownership)}, {0, std::move(target)}, {0, std::move(combat)}};
}

std::vector<gameplay_timed_message> gameplay_monster_attack_sequence(
    const std::uint32_t entity_id,
    const std::uint32_t player_entity_id,
    const std::uint32_t server_skill_id,
    const std::uint8_t action_sequence,
    const gameplay_movement target_position,
    const float facing_degrees,
    const std::uint8_t damage,
    const std::uint64_t remaining_player_health) {
    if (entity_id == 0 || player_entity_id == 0 || server_skill_id == 0 || action_sequence == 0 || damage == 0) {
        throw std::invalid_argument("monster attack is incomplete");
    }

    std::vector<std::uint8_t> cast{0x02, 0x38};
    append_uvarint(cast, entity_id);
    cast.push_back(0);
    append_u32(cast, server_skill_id);
    append_bytes(cast, {action_sequence, 0x02});
    append_uvarint(cast, player_entity_id);
    // Official order: caster facing in degrees, then the target's position.
    append_float(cast, facing_degrees);
    append_float(cast, target_position.x);
    append_float(cast, target_position.y);
    append_float(cast, target_position.z);
    append_bytes(cast, {0x90, 0x4E, 0x01});
    // Trailing u16 captured per tutorial monster skill; unknown skills use the basic attack's.
    append_u16(cast, server_skill_id == 1224240 ? 0x4E90 : server_skill_id == 1224260 ? 0x3EC0 : 0x07E8);

    std::vector<std::uint8_t> hit{0x04, 0x38};
    append_uvarint(hit, player_entity_id);
    append_bytes(hit, {0x06, 0x00});
    append_uvarint(hit, entity_id);
    append_u32(hit, server_skill_id);
    append_bytes(hit, {action_sequence, 0x02, 0x00, 0x00, 0x01, 0xE3, 0x06, 0x4C, 0x07,
                       0x01, 0x00, 0x00, 0x00, 0x90, 0x4E, damage, 0x01, 0x00});

    auto health = encode_gameplay_entity_health(player_entity_id, remaining_player_health);

    std::vector<std::uint8_t> completion{0x06, 0x38};
    append_uvarint(completion, entity_id);
    append_u32(completion, server_skill_id);
    append_bytes(completion, {action_sequence, 0x00});

    return {{0, std::move(cast)}, {250, std::move(hit)}, {0, std::move(health)},
        {500, std::move(completion)}};
}

std::vector<gameplay_timed_message> gameplay_tutorial_light_prelude(
    const std::uint32_t player_entity_id,
    const std::uint64_t server_time_milliseconds,
    const std::uint8_t player_state) {
    std::vector<std::uint8_t> state{0x2C, 0x38};
    append_uvarint(state, player_entity_id);
    append_bytes(state, {0x01, 0x00, player_state, 0x0F});
    return {
        {0, {0x35, 0xE3, 0x00, 0x00, 0x15, 0x43, 0x75, 0x74, 0x73, 0x63, 0x65, 0x6E,
             0x65, 0x5F, 0x4C, 0x5F, 0x41, 0x5F, 0x53, 0x65, 0x71, 0x5F, 0x31, 0x32, 0x37,
             0x38, 0x79, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
        {0, std::move(state)},
        {0, encode_gameplay_tutorial_event_state(1, server_time_milliseconds)},
        {0, {0x02, 0xE0, 0x79, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x06, 0x00, 0x00}},
        {0, {0x04, 0xE0, 0x79, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
        {100, {0x41, 0x8D, 0x00, 0x00}},
    };
}

std::vector<std::uint8_t> encode_gameplay_tutorial_event_state(
    const std::uint32_t state,
    const std::uint64_t server_time_milliseconds) {
    return encode_gameplay_map_event(301100300, state, server_time_milliseconds);
}

std::vector<std::uint8_t> encode_gameplay_tutorial_monster_prompt() {
    return encode_gameplay_tutorial_prompt("Tutorial_Basic_03", "0");
}

std::vector<std::uint8_t> encode_gameplay_tutorial_movement_prompt() {
    return encode_gameplay_tutorial_prompt("Tutorial_Basic_02", "6000");
}

std::vector<std::uint8_t> encode_gameplay_map_event(
    const std::uint32_t event_id,
    const std::uint32_t state,
    const std::uint64_t server_time_milliseconds) {
    std::vector<std::uint8_t> body{0x84, 0x56, 0x01};
    append_u32(body, event_id);
    append_u32(body, state);
    append_u64(body, server_time_milliseconds);
    body.push_back(0);
    return body;
}

std::vector<std::uint8_t> encode_gameplay_quest_step(
    const std::uint32_t quest_instance,
    const std::uint8_t chapter,
    const std::uint8_t step,
    const std::span<const std::uint8_t> objective_flags,
    const std::uint8_t status) {
    if (objective_flags.empty() || objective_flags.size() > 255) throw std::invalid_argument("quest step needs objectives");
    std::vector<std::uint8_t> body{0x2D, 0x92};
    append_u32(body, quest_instance);
    append_bytes(body, {status, chapter, step, static_cast<std::uint8_t>(objective_flags.size())});
    body.insert(body.end(), objective_flags.begin(), objective_flags.end());
    body.push_back(0x08);
    body.insert(body.end(), 16, 0x00);
    return body;
}

std::vector<gameplay_timed_message> gameplay_interaction_sequence(
    const std::uint32_t object_entity_id,
    const std::uint32_t player_entity_id,
    const bool door) {
    std::vector<std::uint8_t> started{0x38, 0x36};
    append_uvarint(started, object_entity_id);
    append_uvarint(started, player_entity_id);
    started.push_back(door ? 0x02 : 0x01);
    std::vector<std::uint8_t> result{0x3A, 0x36};
    append_uvarint(result, object_entity_id);
    append_uvarint(result, player_entity_id);
    result.push_back(door ? 0x01 : 0x00);
    result.insert(result.end(), 7, 0x00);
    auto used = encode_gameplay_object_used(object_entity_id);
    std::vector<std::uint8_t> finished{0x3B, 0x36};
    append_uvarint(finished, object_entity_id);
    std::vector<std::uint8_t> reward{0x37, 0x36, 0x00, 0x00, 0x00};
    append_uvarint(reward, object_entity_id);
    reward.insert(reward.end(), 8, 0x00);
    std::vector<gameplay_timed_message> sequence{{0, std::move(started)}, {0, std::move(result)}, {0, std::move(used)}};
    if (!door) sequence.push_back({0, std::move(finished)});
    sequence.push_back({0, std::move(reward)});
    return sequence;
}

std::vector<std::vector<std::uint8_t>> encode_gameplay_sprint_end(const std::uint32_t player_entity_id) {
    std::vector<std::uint8_t> ended{0x22, 0x37};
    append_uvarint(ended, player_entity_id);
    std::vector<std::uint8_t> released{0x42, 0x37};
    append_uvarint(released, player_entity_id);
    return {std::move(ended), std::move(released)};
}

std::vector<std::uint8_t> encode_gameplay_action_finished(
    const std::uint32_t player_entity_id, const std::uint8_t action_state, const std::uint8_t type) {
    std::vector<std::uint8_t> body{0x2C, 0x38};
    append_uvarint(body, player_entity_id);
    append_bytes(body, {0x01, 0x00, action_state, type});
    return body;
}

std::vector<std::uint8_t> encode_gameplay_jump_landed(
    const std::uint32_t player_entity_id, const std::uint8_t action_state, const std::uint64_t now,
    const gameplay_movement position) {
    return encode_gameplay_movement_action(player_entity_id, action_state, 231, 200, now, position);
}

std::vector<std::uint8_t> encode_gameplay_movement_action(
    const std::uint32_t player_entity_id, const std::uint8_t action_state, const std::uint32_t first,
    const std::uint32_t second, const std::uint64_t now, const gameplay_movement position) {
    std::vector<std::uint8_t> body{0x2A, 0x38};
    append_uvarint(body, player_entity_id);
    append_bytes(body, {0x01, 0x11, action_state});
    append_u32(body, first);
    append_u32(body, second);
    body.insert(body.end(), 4, 0x00);
    append_u64(body, now);
    append_uvarint(body, player_entity_id);
    append_bytes(body, {0x01, 0x00});
    append_float(body, position.x);
    append_float(body, position.y);
    append_float(body, position.z);
    return body;
}

std::vector<std::uint8_t> encode_gameplay_kill_record(
    const std::uint32_t victim_entity_id,
    const std::uint32_t skill_id,
    const std::uint32_t killer_entity_id,
    const std::string_view character_token) {
    if (character_token.size() > 255) throw std::invalid_argument("character token is too long");
    std::vector<std::uint8_t> body{0x04, 0x8D};
    append_uvarint(body, victim_entity_id);
    append_u32(body, skill_id);
    append_uvarint(body, killer_entity_id);
    append_bytes(body, {0x20, 0x05, static_cast<std::uint8_t>(character_token.size())});
    body.insert(body.end(), character_token.begin(), character_token.end());
    body.insert(body.end(), 8, 0x00);
    append_bytes(body, {0x01, 0x00});
    return body;
}

namespace {
constexpr std::uint32_t potion_heal_effect = 0x0BFCB25F;   // 201110111, the heal hit
constexpr std::uint32_t potion_buff_abnormal = 0x0132DEA3; // 20111011, the 10 s buff
constexpr std::uint32_t potion_abnormal_a = 0x0BFCB25F;
constexpr std::uint32_t potion_abnormal_b = 0x0BFCB260;

std::vector<std::uint8_t> potion_abnormal(const std::uint32_t player, const std::uint8_t mode, const std::uint32_t abnormal,
    std::initializer_list<std::uint8_t> payload, const std::uint32_t skill) {
    std::vector<std::uint8_t> body{0x05, 0x38};
    append_uvarint(body, player);
    body.push_back(mode);
    append_uvarint(body, player);
    body.push_back(0x10);
    append_u32(body, abnormal);
    body.insert(body.end(), payload.begin(), payload.end());
    append_u32(body, skill);
    return body;
}
} // namespace

std::vector<gameplay_timed_message> gameplay_potion_use_sequence(
    const std::uint32_t player,
    const std::uint32_t skill,
    const std::uint8_t action_sequence,
    const std::uint8_t action_state,
    const gameplay_inventory_item& remaining,
    const std::uint64_t now,
    const gameplay_movement position) {
    std::vector<std::uint8_t> cast{0x02, 0x38};
    append_uvarint(cast, player);
    cast.push_back(0x00);
    append_u32(cast, skill);
    append_bytes(cast, {action_sequence, 0x00});
    append_uvarint(cast, player);
    append_float(cast, 360.0F);
    append_bytes(cast, {0x90, 0x4E, 0x01, 0x00});
    std::vector<std::uint8_t> completion{0x06, 0x38};
    append_uvarint(completion, player);
    append_u32(completion, skill);
    append_bytes(completion, {action_sequence, 0x00});
    std::vector<std::uint8_t> inventory{0x1B, 0x56, 0x01, 0x01, 0x00};
    append_u64(inventory, remaining.instance_id);
    append_u32(inventory, remaining.template_id);
    append_u32(inventory, remaining.count);
    inventory.insert(inventory.end(), 4, 0x00);
    inventory.push_back(0x01);
    inventory.insert(inventory.end(), 15, 0x00);
    append_bytes(inventory, {0x26, 0x00});
    std::vector<std::uint8_t> used{0x16, 0x56};
    append_u64(used, remaining.instance_id);
    append_u32(used, remaining.template_id);
    std::vector<std::uint8_t> heal{0x04, 0x38};
    append_uvarint(heal, player);
    append_bytes(heal, {0x00, 0x00});
    append_uvarint(heal, player);
    append_u32(heal, skill);
    append_bytes(heal, {action_sequence, 0x02});
    append_u32(heal, potion_heal_effect);
    append_bytes(heal, {0x01, 0x00, 0x00, 0x00, 0x90, 0x4E, 0x01, 0x00});
    std::vector<std::uint8_t> buff{0x2A, 0x38};
    append_uvarint(buff, player);
    append_bytes(buff, {0x01, 0x13, action_state});
    append_u32(buff, potion_buff_abnormal);
    append_u32(buff, 10'000);
    buff.insert(buff.end(), 4, 0x00);
    append_u64(buff, now);
    append_uvarint(buff, player);
    buff.push_back(0x01);
    append_u32(buff, skill);
    buff.push_back(0x00);
    append_float(buff, position.x);
    append_float(buff, position.y);
    append_float(buff, position.z);
    return {{0, std::move(cast)}, {0, std::move(completion)}, {0, std::move(inventory)}, {0, std::move(used)},
        {50, std::move(heal)}, {0, std::move(buff)},
        {0, potion_abnormal(player, 0x09, potion_abnormal_a, {0x30}, skill)},
        {0, potion_abnormal(player, 0x09, potion_abnormal_b, {0x05}, skill)}};
}

std::vector<std::vector<std::uint8_t>> encode_gameplay_potion_tick(
    const std::uint32_t player, const std::uint32_t skill, const std::uint8_t remaining, const std::uint8_t stacks) {
    if (stacks == 0) {
        return {potion_abnormal(player, 0x0B, potion_abnormal_a, {remaining, 0x09}, skill),
            potion_abnormal(player, 0x0A, potion_abnormal_b, {0x01}, skill)};
    }
    return {potion_abnormal(player, 0x0B, potion_abnormal_a, {remaining, 0x09}, skill),
        potion_abnormal(player, 0x0B, potion_abnormal_b, {stacks, 0x01}, skill)};
}

std::vector<std::uint8_t> encode_gameplay_entity_disappear(const std::uint32_t entity_id, const bool destroyed) {
    std::vector<std::uint8_t> body{0x35, 0x36};
    append_uvarint(body, entity_id);
    append_bytes(body, {static_cast<std::uint8_t>(destroyed ? 0x03 : 0x01), 0x03, 0x00});
    return body;
}

std::vector<std::vector<std::uint8_t>> encode_gameplay_object_destroyed(const std::uint32_t object_entity_id) {
    std::vector<std::uint8_t> destroyed{0x93, 0x8D};
    append_uvarint(destroyed, object_entity_id);
    append_bytes(destroyed, {0x01, 0x00, 0x00, 0x00});
    return {std::move(destroyed), encode_gameplay_object_used(object_entity_id)};
}

std::vector<std::uint8_t> rename_character(
    std::vector<std::uint8_t> body, const std::string& from, const std::string& to) {
    if (from.empty() || to.empty() || from.size() > 255 || to.size() > 255) return body;
    std::vector<std::uint8_t> old_name{static_cast<std::uint8_t>(from.size())};
    old_name.insert(old_name.end(), from.begin(), from.end());
    std::vector<std::uint8_t> new_name{static_cast<std::uint8_t>(to.size())};
    new_name.insert(new_name.end(), to.begin(), to.end());
    for (auto at = std::search(body.begin(), body.end(), old_name.begin(), old_name.end()); at != body.end();
         at = std::search(at, body.end(), old_name.begin(), old_name.end())) {
        at = body.erase(at, at + static_cast<std::ptrdiff_t>(old_name.size()));
        at = body.insert(at, new_name.begin(), new_name.end()) + static_cast<std::ptrdiff_t>(new_name.size());
    }
    return body;
}

std::vector<std::vector<std::uint8_t>> encode_gameplay_tutorial_map_end(const std::uint32_t map_id, const std::uint64_t now) {
    std::vector<std::uint8_t> leave{0x01, 0x61, 0x00};
    append_u32(leave, map_id);
    append_bytes(leave, {0x01, 0x00});
    std::vector<std::uint8_t> visit{0x00, 0x61};
    append_u32(visit, map_id);
    visit.push_back(0x03);
    append_u64(visit, now);
    append_u64(visit, now + 600'000);
    return {std::move(leave), std::move(visit)};
}

std::vector<gameplay_timed_message> gameplay_monster_ranged_attack_sequence(
    const std::uint32_t entity_id,
    const std::uint32_t player_entity_id,
    const std::uint32_t server_skill_id,
    const std::uint8_t action_sequence,
    const gameplay_movement target,
    const float facing_degrees,
    const std::uint8_t damage,
    const std::uint64_t remaining_player_health) {
    if (entity_id == 0 || player_entity_id == 0 || server_skill_id == 0 || action_sequence == 0 || damage == 0) {
        throw std::invalid_argument("ranged monster attack is incomplete");
    }
    constexpr std::uint32_t projectile = 0x05F4D6D9;  // official projectile record of skill 1239740
    std::vector<std::uint8_t> cast{0x02, 0x38};
    append_uvarint(cast, entity_id);
    cast.push_back(0x00);
    append_u32(cast, server_skill_id);
    append_bytes(cast, {action_sequence, 0x02});
    append_uvarint(cast, player_entity_id);
    append_float(cast, facing_degrees);
    append_float(cast, target.x);
    append_float(cast, target.y);
    append_float(cast, target.z);
    append_bytes(cast, {0x90, 0x4E, 0x01, 0xE8, 0x07});
    std::vector<std::uint8_t> launch{0x03, 0x38};
    append_uvarint(launch, entity_id);
    launch.push_back(0x00);
    append_uvarint(launch, player_entity_id);
    append_u32(launch, server_skill_id);
    launch.push_back(action_sequence);
    append_u32(launch, projectile);
    append_float(launch, target.x);
    append_float(launch, target.y);
    append_float(launch, target.z);
    append_u16(launch, 962);  // flight time in milliseconds
    append_bytes(launch, {0x01, 0x00, 0x00, 0x00, 0x01, 0x00});
    std::vector<std::uint8_t> hit{0x04, 0x38};
    append_uvarint(hit, player_entity_id);
    append_bytes(hit, {0x06, 0x00});
    append_uvarint(hit, entity_id);
    append_u32(hit, server_skill_id);
    append_bytes(hit, {action_sequence, 0x02, 0x00, 0x00, 0x02, 0x7B, 0xB1, 0x63, 0x07,
                       0x01, 0x00, 0x00, 0x00, 0x90, 0x4E, damage, 0x01, 0x00});
    std::vector<std::uint8_t> impact{0x0E, 0x38};
    append_uvarint(impact, entity_id);
    impact.push_back(0x02);
    append_uvarint(impact, player_entity_id);
    append_u32(impact, server_skill_id);
    impact.push_back(action_sequence);
    append_u32(impact, projectile);
    append_float(impact, target.x);
    append_float(impact, target.y);
    append_float(impact, target.z);
    append_bytes(impact, {0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01});
    append_float(impact, target.x);
    append_float(impact, target.y);
    append_float(impact, target.z);
    std::vector<std::uint8_t> completion{0x06, 0x38};
    append_uvarint(completion, entity_id);
    append_u32(completion, server_skill_id);
    append_bytes(completion, {action_sequence, 0x00});
    return {{0, std::move(cast)}, {900, std::move(launch)}, {50, std::move(hit)}, {0, std::move(impact)},
        {0, encode_gameplay_entity_health(player_entity_id, remaining_player_health)}, {700, std::move(completion)}};
}

std::vector<std::uint8_t> encode_gameplay_object_used(const std::uint32_t object_entity_id) {
    std::vector<std::uint8_t> body{0x6D, 0x8D};
    append_uvarint(body, object_entity_id);
    return body;
}

std::vector<std::uint8_t> encode_gameplay_tutorial_prompt(const std::string_view name, const std::string_view parameter) {
    if (name.size() > 255 || parameter.size() > 255) throw std::invalid_argument("tutorial prompt is too long");
    std::vector<std::uint8_t> body{0x15, 0x8D, 0x00, 0x2E, static_cast<std::uint8_t>(name.size())};
    body.insert(body.end(), name.begin(), name.end());
    body.insert(body.end(), 13, 0x00);
    body.push_back(static_cast<std::uint8_t>(parameter.size()));
    body.insert(body.end(), parameter.begin(), parameter.end());
    return body;
}

std::vector<std::uint8_t> encode_gameplay_talk_prompt(const std::string_view name, const std::uint32_t speaker_entity_id) {
    if (name.size() > 255) throw std::invalid_argument("talk prompt is too long");
    std::vector<std::uint8_t> body{0x15, 0x8D, static_cast<std::uint8_t>(speaker_entity_id ? 0x01 : 0x00),
        static_cast<std::uint8_t>(speaker_entity_id ? 0x2B : 0x13), static_cast<std::uint8_t>(name.size())};
    body.insert(body.end(), name.begin(), name.end());
    if (speaker_entity_id) {
        body.insert(body.end(), 13, 0x00);
        body.push_back(0x01);
        append_uvarint(body, speaker_entity_id);
        body.push_back(0x00);
    } else {
        body.insert(body.end(), 9, 0x00);
        append_bytes(body, {0x01, 0x00, 0x00, 0x00, 0x00});
    }
    return body;
}

std::vector<std::uint8_t> encode_gameplay_target_selected(const std::uint32_t target_entity_id) {
    std::vector<std::uint8_t> body{0x34, 0x38, 0x00, 0x00, 0x00};
    append_uvarint(body, target_entity_id);
    return body;
}

std::vector<std::uint8_t> encode_gameplay_target_state(const std::uint32_t target_entity_id) {
    std::vector<std::uint8_t> body{0x31, 0x38, 0x00, 0x00};
    append_uvarint(body, target_entity_id);
    append_bytes(body, {0x00, 0x00});
    return body;
}

gameplay_interaction parse_gameplay_interaction(const std::span<const std::uint8_t> body) {
    if (body.size() < 8 || !has_gameplay_opcode(body, gameplay_client_message::tutorial_objective_progress)) {
        throw std::runtime_error("frame is not an interaction");
    }
    std::size_t offset = 3;
    const auto object = read_uvarint(body, offset);
    return {object, read_u32(body, body.size() - 4)};
}

std::vector<std::vector<std::uint8_t>> unpack_gameplay_snapshot(const std::span<const std::uint8_t> body) {
    if (body.size() < 6 || body[0] != 0xFF || body[1] != 0xFF) throw std::runtime_error("frame is not a snapshot");
    const auto size = read_u32(body, 2);
    std::vector<std::uint8_t> data;
    data.reserve(size);
    std::size_t i = 6;
    const auto length = [&](std::size_t value) {
        if (value == 15) {
            std::uint8_t more = 255;
            while (more == 255 && i < body.size()) value += more = body[i++];
        }
        return value;
    };
    while (i < body.size()) {
        const auto token = body[i++];
        const auto literals = length(token >> 4);
        if (i + literals > body.size()) throw std::runtime_error("snapshot literal run is truncated");
        data.insert(data.end(), body.begin() + static_cast<std::ptrdiff_t>(i),
            body.begin() + static_cast<std::ptrdiff_t>(i + literals));
        i += literals;
        if (i >= body.size()) break;
        if (i + 2 > body.size()) throw std::runtime_error("snapshot match is truncated");
        const std::size_t offset = body[i] | static_cast<std::size_t>(body[i + 1]) << 8;
        i += 2;
        const auto match = length(token & 0x0F) + 4;
        if (offset == 0 || offset > data.size()) throw std::runtime_error("snapshot match offset is invalid");
        const auto start = data.size() - offset;
        for (std::size_t k = 0; k < match; ++k) data.push_back(data[start + k]);
    }
    if (data.size() != size) throw std::runtime_error("snapshot size mismatch");
    std::vector<std::vector<std::uint8_t>> result;
    std::size_t offset = 0;
    while (offset < data.size()) {
        const auto framed = read_uvarint(data, offset);
        if (framed < 4 || offset + framed - 4 > data.size()) throw std::runtime_error("snapshot record is truncated");
        result.emplace_back(data.begin() + static_cast<std::ptrdiff_t>(offset),
            data.begin() + static_cast<std::ptrdiff_t>(offset + framed - 4));
        offset += framed - 4;
    }
    return result;
}

std::uint32_t parse_gameplay_target_request(const std::span<const std::uint8_t> body) {
    if (body.size() < 4) throw std::runtime_error("target request is truncated");
    std::size_t offset = 3;
    return read_uvarint(body, offset);
}

} // namespace sema
