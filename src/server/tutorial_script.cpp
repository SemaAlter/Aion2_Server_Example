#include "tutorial_script.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using message_list = std::vector<sema::gameplay_timed_message>;

struct checkpoint_position {
    float x;
    float y;
};

// Arrival Point positions of the official run, in checkpoint order.
constexpr std::array<checkpoint_position, 9> checkpoint_positions{{
    {-97703.0F, 54664.0F},  // movement (WASD task)
    {-95931.0F, 54664.0F},  // light (Seq_1278 handoff)
    {-88237.0F, 54664.0F},  // corridor
    {-84058.0F, 54683.0F},  // corridor end
    {-78885.0F, 56755.0F},  // second encounter
    {-76406.0F, 64479.0F},  // test-tube room
    {-73355.0F, 64395.0F},  // finale
    {-71455.0F, 64395.0F},  // room point A
    {-69917.0F, 64467.0F},  // room point B
}};

// Freed captive (EnvObj 1400225 tube): the official spawn of NPC 2970192, entity 20371
// (official-tutorial-full 223.52 s), and its action end 3.4 s later.
constexpr std::string_view captive_spawn_hex =
    "4136939F0104300050522D00000204918CC7A77478470086BB460000344200200185018501640000006400000000000000"
    "000000000000000000000000640000006400000001000000000000000000000000000000000000000E01110181969800"
    "FFFFFFFFFFFFFFFF8075D52ABB030000939F01010004918CC7A77478470086BB46060001000000AE4A0F00AC84B7B300";
constexpr std::string_view captive_action_hex = "2C38939F0101000201";

std::vector<std::uint8_t> from_hex(const std::string_view text) {
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        bytes.push_back(static_cast<std::uint8_t>(std::stoul(std::string(text.substr(i, 2)), nullptr, 16)));
    }
    return bytes;
}
constexpr float checkpoint_tolerance = 300.0F;

// Group 2 and 3 entities of the official run; spawns.csv uses the same IDs.
constexpr std::uint32_t encounter_speaker = 55173;
constexpr std::uint32_t encounter_officer = 41778;
constexpr std::uint32_t finale_speakers[] = {34583, 41456, 41228};

constexpr std::uint32_t kill_counter_events[] = {311100100, 311100200};
constexpr std::uint32_t combo_kill_event = 310100100;

std::uint32_t read_uvarint(const std::vector<std::uint8_t>& bytes, std::size_t& offset) {
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 35 && offset < bytes.size(); shift += 7) {
        const auto byte = bytes[offset++];
        value |= static_cast<std::uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) return value;
    }
    throw std::runtime_error("bootstrap varint is invalid");
}

std::vector<std::uint8_t> uvarint(std::uint32_t value) {
    std::vector<std::uint8_t> bytes;
    do {
        auto byte = static_cast<std::uint8_t>(value & 0x7F);
        value >>= 7;
        if (value != 0) byte |= 0x80;
        bytes.push_back(byte);
    } while (value != 0);
    return bytes;
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(bytes[offset + i]) << (8 * i);
    return value;
}

void append(message_list& target, message_list messages, const std::uint32_t first_delay = 0) {
    if (messages.empty()) return;
    messages.front().delay_milliseconds += first_delay;
    target.insert(target.end(), std::make_move_iterator(messages.begin()), std::make_move_iterator(messages.end()));
}

void append(message_list& target, std::vector<std::uint8_t> body, const std::uint32_t delay = 0) {
    target.push_back({delay, std::move(body)});
}


// Character tokens are length-prefixed: 0C '$' then 11 alphanumerics.
std::size_t token_at(const std::vector<std::uint8_t>& body, const std::size_t offset) {
    if (offset + 13 > body.size() || body[offset] != 0x0C || body[offset + 1] != '$') return 0;
    for (std::size_t i = offset + 2; i < offset + 13; ++i) {
        const auto c = body[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return 0;
    }
    return 12;
}

std::vector<std::uint8_t> quest(const std::uint32_t instance, const std::uint8_t chapter, const std::uint8_t step,
    const std::vector<std::uint8_t>& flags, const std::uint8_t status = 0x02) {
    return sema::encode_gameplay_quest_step(instance, chapter, step, flags, status);
}

} // namespace

tutorial_script::tutorial_script(
    const replay_scenario& bootstrap,
    std::optional<replay_phase> room_spawns,
    std::optional<replay_phase> exit)
    : room_spawns_(std::move(room_spawns)), exit_(std::move(exit)) {
    if (room_spawns_) {
        for (const auto& event : room_spawns_->events) {
            const auto& body = event.body;
            if (body.size() < 20 || body[0] != 0x34 || body[1] != 0x36) continue;
            std::size_t offset = 2;
            const auto entity = read_uvarint(body, offset);
            const auto template_id = read_u32(body, offset + 2);
            room_objects_.push_back(entity);
            if (template_id == darkened_tube_template || template_id == captive_tube_template
                || template_id == balaur_tube_template) {
                tubes_.push_back({entity, template_id, std::bit_cast<float>(read_u32(body, offset + 6)),
                    std::bit_cast<float>(read_u32(body, offset + 10))});
            }
        }
    }
    for (const auto& event : bootstrap.events) {
        std::vector<std::vector<std::uint8_t>> bodies;
        if (event.body.size() >= 6 && event.body[0] == 0xFF && event.body[1] == 0xFF) {
            bodies = sema::unpack_gameplay_snapshot(event.body);
        } else {
            bodies.emplace_back(event.body.begin(), event.body.end());
        }
        for (const auto& body : bodies) {
            if (body.size() >= 6 && body[0] == 0x2D && body[1] == 0x92 && quest_instance_ == 0) {
                quest_instance_ = read_u32(body, 2);
            } else if (body.size() > 20 && body[0] == 0x11 && body[1] == 0x56 && !potion_) {
                // Inventory: instance u64, template u32, count u32 per item.
                for (std::size_t i = 8; i + 8 <= body.size() && !potion_; ++i) {
                    if (read_u32(body, i) != life_potion_template) continue;
                    std::uint64_t instance = 0;
                    for (std::size_t k = 0; k < 8; ++k) instance |= static_cast<std::uint64_t>(body[i - 8 + k]) << (8 * k);
                    potion_ = sema::gameplay_inventory_item{instance, life_potion_template, read_u32(body, i + 4)};
                }
            } else if (body.size() > 20 && body[0] == 0x33 && body[1] == 0x36 && character_token_.empty()) {
                character_record_ = body;
                for (std::size_t i = 2; i + 13 <= body.size() && character_token_.empty(); ++i) {
                    if (token_at(body, i)) character_token_.assign(body.begin() + static_cast<std::ptrdiff_t>(i + 1),
                        body.begin() + static_cast<std::ptrdiff_t>(i + 13));
                }
            } else if (body.size() >= 20 && body[0] == 0x34 && body[1] == 0x36) {
                std::size_t offset = 2;
                const auto entity = read_uvarint(body, offset);
                if (offset + 14 > body.size()) continue;
                const auto template_id = read_u32(body, offset + 2);
                objects_.push_back({entity, template_id, std::bit_cast<float>(read_u32(body, offset + 6)),
                    std::bit_cast<float>(read_u32(body, offset + 10))});
            }
        }
    }
    if (quest_instance_ == 0) throw std::runtime_error("tutorial bootstrap has no quest step record");
}

std::optional<sema::gameplay_inventory_item> tutorial_script::use_potion() noexcept {
    if (!potion_ || potion_->count == 0) return std::nullopt;
    --potion_->count;
    return potion_;
}

std::optional<tutorial_script::checkpoint> tutorial_script::checkpoint_of(const std::uint32_t object_entity_id) const noexcept {
    const auto object = std::ranges::find(objects_, object_entity_id, &environment_object::entity_id);
    if (object == objects_.end() || object->template_id != arrival_point_template) return std::nullopt;
    for (std::size_t index = 0; index < checkpoint_positions.size(); ++index) {
        if (std::hypot(object->x - checkpoint_positions[index].x, object->y - checkpoint_positions[index].y)
            <= checkpoint_tolerance) {
            return static_cast<checkpoint>(index);
        }
    }
    return std::nullopt;
}

void tutorial_script::retoken(std::vector<std::uint8_t>& body) const {
    if (character_token_.size() != 12) return;
    for (std::size_t i = 0; i + 13 <= body.size(); ++i) {
        if (token_at(body, i)) std::copy(character_token_.begin(), character_token_.end(), body.begin() + static_cast<std::ptrdiff_t>(i + 1));
    }
}

std::vector<sema::gameplay_timed_message> tutorial_script::replay(
    const replay_phase& phase, const std::uint32_t player_entity_id, const std::uint64_t now) const {
    std::vector<sema::gameplay_timed_message> result;
    for (auto& event : prepare_replay_phase(phase, player_entity_id, now)) {
        retoken(event.body);
        result.push_back({static_cast<std::uint32_t>(event.delay.count()), std::move(event.body)});
    }
    return result;
}

void tutorial_script::adapt_handoff(std::vector<replay_event>& events, const std::uint8_t cutscene_action) const {
    std::uint32_t source_light_object = 0;
    for (const auto& event : events) {
        if (event.body.size() > 3 && event.body[0] == 0x38 && event.body[1] == 0x36) {
            std::size_t offset = 2;
            source_light_object = read_uvarint(event.body, offset);
            break;
        }
    }
    std::uint32_t light_object = 0;
    for (const auto& object : objects_) {
        if (checkpoint_of(object.entity_id) == checkpoint::light) light_object = object.entity_id;
    }
    const auto source = uvarint(source_light_object);
    const auto target = uvarint(light_object);
    for (auto& event : events) {
        auto& body = event.body;
        if (body.size() > 2 && body[0] == 0x33 && body[1] == 0x36 && !character_record_.empty()) {
            body = character_record_;
            continue;
        }
        retoken(body);
        if (body.size() > 6 && (body[0] == 0x2A || body[0] == 0x2B) && body[1] == 0x38) {
            std::size_t offset = 2;
            (void)read_uvarint(body, offset);
            if (body[0] == 0x2A && offset + 2 < body.size() && body[offset] == 0x01) ++offset;  // record count
            if (offset + 1 < body.size() && (body[offset] == 0x11 || body[offset] == 0x13)) body[offset + 1] = cutscene_action;
        }
        if (body.size() >= 6 && body[0] == 0x2D && body[1] == 0x92) {
            for (std::size_t i = 0; i < 4; ++i) body[2 + i] = static_cast<std::uint8_t>(quest_instance_ >> (8 * i));
        }
        if (light_object == 0 || source_light_object == 0 || source.size() != target.size()) continue;
        for (std::size_t i = 0; i + source.size() <= body.size(); ++i) {
            if (std::equal(source.begin(), source.end(), body.begin() + static_cast<std::ptrdiff_t>(i))) {
                std::ranges::copy(target, body.begin() + static_cast<std::ptrdiff_t>(i));
                i += target.size() - 1;
            }
        }
    }
}

std::vector<sema::gameplay_timed_message> tutorial_script::on_interaction(
    const sema::gameplay_interaction& interaction,
    const std::uint32_t player_entity_id,
    tutorial_world& world,
    const std::uint64_t now) {
    message_list result;
    const auto object = interaction.object_entity_id;
    const auto chain = [&] { return sema::gameplay_interaction_sequence(object, player_entity_id, false); };

    if (interaction.template_id == castle_door_template) {
        if (door_done_ || !checkpoint_done_[static_cast<std::size_t>(checkpoint::room)]) return result;
        door_done_ = true;
        append(result, sema::encode_gameplay_target_selected(object));
        append(result, sema::gameplay_interaction_sequence(object, player_entity_id, true), 50);
        append(result, quest(quest_instance_, 2, 4, {1, 1}));
        append(result, quest(quest_instance_, 3, 1, {0, 0, 0, 0, 0, 0}));
        append(result, world.spawn_group(3, player_entity_id, false, true), 1'800);
        return result;
    }
    if (interaction.template_id != arrival_point_template) return result;
    const auto reached = checkpoint_of(object);
    if (!reached) return result;
    if (*reached == checkpoint::room_a || *reached == checkpoint::room_b) {
        // Order, not position, decides: H after the finale, the exit after quest step 3/3 begins.
        if (checkpoint_done_[static_cast<std::size_t>(checkpoint::finale)] && !checkpoint_h_done_) {
            checkpoint_h_done_ = true;
            room_flags_[1] = 1;
            append(result, chain());
            append(result, quest(quest_instance_, 3, 1, room_flags_));
            append(result, sema::encode_gameplay_tutorial_prompt("Tutorial_Basic_08", "15000"));
            world.engage_group(3);  // activation records follow from the monster AI
        } else if (exit_step_started_ && !tutorial_finished_) {
            tutorial_finished_ = true;
            finished_ms_ = now;
            append(result, chain());
            append(result, quest(quest_instance_, 3, 3, {1}));
            for (auto& body : sema::encode_gameplay_tutorial_map_end(100001, now)) {
                append(result, std::move(body), 50);
            }
            append(result, quest(quest_instance_, 3, 3, {1}, 0x03));
            // The client answers with 17 36 (leave_tutorial_request); leave() sends the exit.
        }
        return result;
    }
    const auto index = static_cast<std::size_t>(*reached);
    if (checkpoint_done_[index]) return result;
    // Each step requires the previous one; a checkpoint reached early fires when the player returns.
    const bool ready = [&] {
        switch (*reached) {
        case checkpoint::movement: return true;
        case checkpoint::light: return checkpoint_done_[0];
        case checkpoint::corridor: return encounter1_completed_;
        case checkpoint::corridor_end: return checkpoint_done_[2];
        case checkpoint::encounter: return checkpoint_done_[3];
        case checkpoint::room: return encounter2_completed_;
        case checkpoint::finale: return door_done_;
        case checkpoint::room_a:
        case checkpoint::room_b: return false;  // handled above
        }
        return false;
    }();
    if (!ready) return result;
    checkpoint_done_[index] = true;

    switch (*reached) {
    case checkpoint::movement:
        append(result, chain());
        append(result, quest(quest_instance_, 1, 3, {1}));
        append(result, quest(quest_instance_, 1, 4, {0}));
        append(result, sema::encode_gameplay_tutorial_prompt("Tutorial_Basic_02", "6000"));
        break;
    case checkpoint::light:
        break;  // the light transition (movement trigger and cutscene handoff) answers it
    case checkpoint::corridor:
        append(result, chain());
        append(result, quest(quest_instance_, 2, 1, {1, 0}));
        append(result, sema::encode_gameplay_tutorial_prompt("Tutorial_Basic_05", "0"));
        break;
    case checkpoint::corridor_end:
        append(result, chain());
        append(result, quest(quest_instance_, 2, 1, {1, 1}));
        append(result, quest(quest_instance_, 2, 2, {0}));
        append(result, sema::encode_gameplay_talk_prompt("Talk_L_Starter_InstanceLayer_1_05", 0));
        append(result, sema::encode_gameplay_tutorial_prompt("Tutorial_Basic_06", "10000"), 90);
        break;
    case checkpoint::encounter:
        append(result, world.spawn_group(2, player_entity_id, false, false));
        append(result, chain());
        append(result, quest(quest_instance_, 2, 2, {1}));
        append(result, quest(quest_instance_, 2, 3, {0, 0, 0}));
        append(result, sema::encode_gameplay_talk_prompt("Talk_L_Starter_InstanceLayer_1_06", 0));
        append(result, sema::encode_gameplay_tutorial_prompt("Tutorial_Basic_04", "0"), 100);
        append(result, sema::encode_gameplay_talk_prompt("Talk_L_Starter_InstanceLayer_1_32", encounter_speaker), 100);
        append(result, sema::encode_gameplay_talk_prompt("Talk_L_Starter_InstanceLayer_1_31", encounter_officer), 110);
        break;
    case checkpoint::room:
        append(result, chain());
        append(result, quest(quest_instance_, 2, 4, {1, 0}));
        append(result, sema::encode_gameplay_tutorial_prompt("Tutorial_Basic_07", "0"));
        break;
    case checkpoint::finale:
        room_flags_[0] = 1;
        append(result, chain());
        append(result, quest(quest_instance_, 3, 1, room_flags_));
        append(result, sema::encode_gameplay_talk_prompt("Talk_L_Starter_InstanceLayer_1_21", finale_speakers[0]));
        append(result, sema::encode_gameplay_talk_prompt("Talk_L_Starter_InstanceLayer_1_35", finale_speakers[1]), 100);
        append(result, sema::encode_gameplay_talk_prompt("Talk_L_Starter_InstanceLayer_1_34", finale_speakers[2]), 100);
        append(result, sema::encode_gameplay_talk_prompt("Talk_L_Starter_InstanceLayer_1_08", 0), 100);
        break;
    case checkpoint::room_a:
    case checkpoint::room_b:
        break;
    }
    return result;
}

std::vector<sema::gameplay_timed_message> tutorial_script::on_attack(
    const sema::gameplay_movement position,
    const float facing_degrees,
    const float range_world_units,
    const float angle_degrees,
    const bool frontal,
    const std::uint32_t player_entity_id,
    tutorial_world& world,
    const std::uint64_t now) {
    message_list result;
    if (range_world_units <= 0.0F) return result;
    const auto heading = facing_degrees * 3.14159265F / 180.0F;
    const auto half_angle_cos = std::cos(angle_degrees * 3.14159265F / 360.0F);
    // Tubes are props around the attack's reach; allow their collision radius (125) too.
    const auto reach = range_world_units + 125.0F;
    for (auto& tube : tubes_) {
        if (tube.broken) continue;
        const auto dx = tube.x - position.x;
        const auto dy = tube.y - position.y;
        const auto distance = std::hypot(dx, dy);
        if (distance > reach) continue;
        if (frontal && distance > 1.0F && (dx * std::cos(heading) + dy * std::sin(heading)) / distance < half_angle_cos) {
            continue;
        }
        tube.broken = true;
        for (auto& body : sema::encode_gameplay_object_destroyed(tube.entity_id)) append(result, std::move(body), result.empty() ? 300U : 0U);
        if (tube.template_id == captive_tube_template) {
            tube_flags_[0] = 1;
            ++tube_flags_[3];
            if (tube_step_started_) append(result, quest(quest_instance_, 3, 2, tube_flags_), 50);
            append(result, from_hex(captive_spawn_hex), 100);
            append(result, from_hex(captive_action_hex), 3'400);
        }
    }
    // The Balaur emerges from its tube once the room's pack is dead.
    append(result, release_boss(player_entity_id, world, now));
    return result;
}

std::vector<sema::gameplay_timed_message> tutorial_script::leave(
    const std::uint32_t player_entity_id, const std::uint64_t now) {
    if (!exit_ || leave_answered_) return {};
    leave_answered_ = true;
    left_ = true;
    auto exit = replay(*exit_, player_entity_id, now);
    // 25 36 arrives ~30 ms after 00 61, while quest 3/3 completion may still be queued; the
    // official exit follows 0.12 s later, after the client has seen the quest complete.
    if (!exit.empty()) exit.front().delay_milliseconds = exit_answer_delay_milliseconds;
    // Then the room's objects leave, destroyed tubes flagged (official 0.21 s after the roster).
    for (const auto object : room_objects_) {
        const auto found = std::ranges::find(tubes_, object, &tube::entity_id);
        exit.push_back({object == room_objects_.front() ? 210U : 0U,
            sema::encode_gameplay_entity_disappear(object, found != tubes_.end() && found->broken)});
    }
    return exit;
}

std::vector<sema::gameplay_timed_message> tutorial_script::leave_if_overdue(
    const std::uint32_t player_entity_id, const std::uint64_t now) {
    // Fallback in case the client never sends 17 36; official answers it within 0.1 s.
    if (!exit_ || !tutorial_finished_ || left_ || now < finished_ms_ + leave_fallback_milliseconds) return {};
    left_ = true;
    return replay(*exit_, player_entity_id, now);
}

std::vector<sema::gameplay_timed_message> tutorial_script::release_boss(
    const std::uint32_t player_entity_id, tutorial_world& world, const std::uint64_t now) {
    message_list result;
    const bool balaur_tube_broken = std::ranges::any_of(tubes_, [](const tube& candidate) {
        return candidate.template_id == balaur_tube_template && candidate.broken;
    });
    if (boss_released_ || !balaur_tube_broken || !world.group_cleared(3)) return result;
    boss_released_ = true;
    tube_flags_[1] = 1;
    ++tube_flags_[3];
    append(result, quest(quest_instance_, 3, 2, tube_flags_), 50);
    // Official-tutorial-full 232.28 s: 41 36 spawn, the 2A 38 emergence action (9999506, 4933 ms),
    // the Balaur's line, its aggro (35 38 / 21 8D), and 2C 38 when it steps out 5 s later.
    auto spawn = world.spawn_group(4, player_entity_id, true, false);
    for (const auto boss : world.group_members(4)) {
        const auto info = world.monster(boss);
        if (!info || spawn.empty()) continue;
        spawn.insert(spawn.begin() + 1, {0, sema::encode_gameplay_movement_action(boss, 0x01, boss_emerge_action,
            boss_emerge_milliseconds, now, {info->position.x, info->position.y, info->position.z})});
        spawn.insert(spawn.begin() + 2, {0, sema::encode_gameplay_talk_prompt("Talk_L_Starter_InstanceLayer_1_20", 0)});
        spawn.push_back({5'000, sema::encode_gameplay_action_finished(boss, 0x01, 0x01)});
        world.hold(boss, 5'000);
    }
    append(result, std::move(spawn), 100);
    return result;
}

std::vector<sema::gameplay_timed_message> tutorial_script::on_kills(
    const std::span<const sema::gameplay_attack_target> targets,
    const std::uint32_t skill_id,
    const std::uint32_t player_entity_id,
    tutorial_world& world,
    const std::uint64_t now) {
    message_list result;
    for (const auto& target : targets) {
        if (!target.killed) continue;
        const auto info = world.monster(target.entity_id);
        if (!info) continue;
        ++kills_;
        append(result, sema::encode_gameplay_kill_record(target.entity_id, skill_id, player_entity_id, character_token_));
        for (const auto event : kill_counter_events) append(result, sema::encode_gameplay_map_event(event, kills_, now));
        // 310100100 matched primary-combo kills in the official first encounter;
        // its exact rule is unknown.
        if (skill_id == 9865 || skill_id == 9866) {
            append(result, sema::encode_gameplay_map_event(combo_kill_event, ++combo_kills_, now));
        }
        if (info->group == 1 && info->index_in_group < encounter1_flags_.size()) {
            encounter1_flags_[info->index_in_group] = 1;
            append(result, quest(quest_instance_, 1, 5, encounter1_flags_), 50);
        } else if (info->group == 2) {
            // Objectives: every 2701692, the 2701693 officer, the 2701091.
            std::map<std::uint32_t, std::pair<int, int>> by_template;  // template -> (alive, total)
            for (const auto member : world.group_members(2)) {
                const auto member_info = world.monster(member);
                if (!member_info) continue;
                auto& counts = by_template[member_info->template_id];
                ++counts.second;
                if (!world.is_dead(member)) ++counts.first;
            }
            const auto done = [&](const std::uint32_t template_id) -> std::uint8_t {
                const auto found = by_template.find(template_id);
                return found != by_template.end() && found->second.first == 0 ? 1 : 0;
            };
            const std::vector<std::uint8_t> flags{done(2701692), done(2701693), done(2701091)};
            if (flags != encounter2_flags_) {
                encounter2_flags_ = flags;
                append(result, quest(quest_instance_, 2, 3, encounter2_flags_), 50);
            }
        } else if (info->group == 3) {
            // Objectives after the two checkpoints: both 2702050 casters, the 2701692 and 2701693,
            // every 2701091, and the first 2701091 (official kill order 217-222 s).
            int alive_casters = 0, alive_pair = 0, alive_soldiers = 0, dead_soldiers = 0;
            for (const auto member : world.group_members(3)) {
                const auto member_info = world.monster(member);
                if (!member_info) continue;
                const bool dead = world.is_dead(member);
                if (member_info->template_id == 2702050) alive_casters += dead ? 0 : 1;
                else if (member_info->template_id == 2701091) (dead ? dead_soldiers : alive_soldiers) += 1;
                else alive_pair += dead ? 0 : 1;
            }
            auto flags = room_flags_;
            flags[2] = alive_casters == 0;
            flags[3] = alive_pair == 0;
            flags[4] = alive_soldiers == 0;
            flags[5] = dead_soldiers > 0;
            if (flags != room_flags_) {
                room_flags_ = flags;
                append(result, quest(quest_instance_, 3, 1, room_flags_), 50);
            }
        } else if (info->group == 4 && tube_flags_[2] == 0) {
            tube_flags_[2] = 1;
            append(result, quest(quest_instance_, 3, 2, tube_flags_), 50);
            append(result, quest(quest_instance_, 3, 3, {0}));
            exit_step_started_ = true;
        }
    }
    if (!tube_step_started_ && checkpoint_h_done_ && world.group_cleared(3)) {
        tube_step_started_ = true;
        append(result, quest(quest_instance_, 3, 2, tube_flags_));
        append(result, release_boss(player_entity_id, world, now));
    }
    if (!encounter1_completed_ && world.group_cleared(1)) {
        encounter1_completed_ = true;
        // The Blocked Door at the end of the first corridor opens on its own object-used record.
        const auto door = std::ranges::find(objects_, blocked_door_template, &environment_object::template_id);
        if (door != objects_.end()) append(result, sema::encode_gameplay_object_used(door->entity_id), 50);
        append(result, quest(quest_instance_, 2, 1, {0, 0}), 100);
        append(result, sema::encode_gameplay_talk_prompt("Talk_L_Starter_InstanceLayer_1_04", 0), 50);
    }
    if (!encounter2_completed_ && world.group_cleared(2)) {
        encounter2_completed_ = true;
        append(result, quest(quest_instance_, 2, 4, {0, 0}), 50);
        // The room behind the castle door fills before the player walks in (official: ~10 s after
        // the last kill, while the player crosses the corridor).
        if (room_spawns_) append(result, replay(*room_spawns_, player_entity_id, now + 3'000), 3'000);
    }
    return result;
}
