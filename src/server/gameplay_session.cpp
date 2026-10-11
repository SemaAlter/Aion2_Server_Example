#include "gameplay_session.hpp"

#include "sema/gameplay.hpp"
#include "sema/handshake.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace {
// First length-prefixed name in a request (u8 length, 2-16 bytes, no control characters).
std::string request_name(const std::span<const std::uint8_t> body) {
    for (std::size_t i = 2; i < body.size(); ++i) {
        const std::size_t length = body[i];
        if (length < 2 || length > 16 || i + 1 + length > body.size()) continue;
        if (std::all_of(body.begin() + static_cast<std::ptrdiff_t>(i + 1),
                body.begin() + static_cast<std::ptrdiff_t>(i + 1 + length),
                [](const std::uint8_t byte) { return byte >= 0x20 && byte != 0x7F; })) {
            return {body.begin() + static_cast<std::ptrdiff_t>(i + 1), body.begin() + static_cast<std::ptrdiff_t>(i + 1 + length)};
        }
    }
    return {};
}

// The client settles inside the light near X -95281 and does not continue to
// the old -95000 boundary. Fire on entry, before client-side movement locks.
constexpr float tutorial_cutscene_x = -95600.0F;
}

gameplay_session::gameplay_session(
    const std::uint16_t server_id,
    std::vector<std::uint8_t> session_data,
    const skill_catalog& skills)
    : server_id_(server_id), session_data_(std::move(session_data)), skills_(skills) {
    if (server_id_ == 0 || session_data_.empty()) throw std::invalid_argument("gameplay session configuration is incomplete");
}

bool gameplay_session::consume_tutorial_request() noexcept {
    const auto result = tutorial_requested_;
    tutorial_requested_ = false;
    return result;
}

std::vector<sema::gameplay_interaction> gameplay_session::consume_tutorial_interactions() {
    return std::exchange(tutorial_interactions_, {});
}

bool gameplay_session::consume_tutorial_light_trigger() noexcept {
    const auto result = tutorial_light_triggered_;
    tutorial_light_triggered_ = false;
    return result;
}

bool gameplay_session::consume_tutorial_cutscene_finished() noexcept {
    const auto result = tutorial_cutscene_finished_;
    tutorial_cutscene_finished_ = false;
    return result;
}

std::optional<tutorial_cast_action> gameplay_session::consume_tutorial_dash() noexcept {
    auto result = tutorial_dash_requested_;
    tutorial_dash_requested_.reset();
    return result;
}

std::optional<tutorial_cast_action> gameplay_session::consume_tutorial_attack() noexcept {
    auto result = tutorial_attack_requested_;
    tutorial_attack_requested_.reset();
    return result;
}

bool gameplay_session::consume_sprint_interrupted() noexcept {
    return std::exchange(sprint_interrupted_, false);
}

bool gameplay_session::consume_tutorial_leave_request() noexcept {
    return std::exchange(tutorial_leave_requested_, false);
}

bool gameplay_session::consume_tutorial_takeoff() noexcept {
    return std::exchange(took_off_, false);
}

bool gameplay_session::consume_tutorial_landing() noexcept {
    return std::exchange(landed_, false);
}

std::optional<tutorial_cast_action> gameplay_session::consume_tutorial_potion() noexcept {
    return std::exchange(tutorial_potion_requested_, std::nullopt);
}

std::uint8_t gameplay_session::allocate_action_sequence() noexcept {
    const auto result = next_action_sequence_++;
    if (next_action_sequence_ == 0) next_action_sequence_ = 1;
    return result;
}

std::vector<std::uint8_t> gameplay_session::respond(const std::span<const std::uint8_t> client_body) {
    ++sequence_;
    if (sequence_ == 1) {
        return sema::create_server_hello(sema::parse_client_hello(client_body), session_data_);
    }
    if (!authenticated_) {
        const auto client = sema::parse_gameplay_client_login(client_body);
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        authenticated_ = true;
        return sema::encode_empty_gameplay_login(client, server_id_, static_cast<std::uint64_t>(now), login_characters_);
    }

    if (sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::ping)) {
        const auto ping = sema::parse_gameplay_ping(client_body);
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return sema::encode_gameplay_ping(ping, static_cast<std::uint64_t>(now));
    }
    if (sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::exit_game_request)) {
        return sema::encode_gameplay_exit_result();
    }
    if (sema::has_gameplay_opcode(
            client_body, sema::gameplay_client_message::return_to_character_selection)) {
        // In the tutorial the official server answers with the exit (47 36 0A 00, the 26 36 roster)
        // and the client closes on its own after the end cutscene (official-tutorial-full 249.31 s).
        if (tutorial_started_) tutorial_leave_requested_ = true;
        else close_requested_ = true;
        return {};
    }
    if (tutorial_started_
        && sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::leave_tutorial_request)) {
        tutorial_leave_requested_ = true;
        return {};
    }
    if (tutorial_started_) {
        if (sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::movement_stopped)
            || sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::movement_snapshot)
            || sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::tutorial_objective_progress)) {
            sprint_interrupted_ = true;
        }
        if (sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::air_movement_started)
            || sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::air_movement_updated)) {
            if (!airborne_) took_off_ = true;
            airborne_ = true;
        } else if (airborne_
            && (sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::air_movement_stopped)
                || sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::movement_started)
                || sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::movement_updated)
                || sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::movement_stopped))) {
            airborne_ = false;
            landed_ = true;
        }
    }
    if (!tutorial_started_ && sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::character_name_check)) {
        // Official 1A 36 00 00 <name> 01 (official character creation): the name is free.
        if (auto name = request_name(client_body); !name.empty()) character_name_ = std::move(name);
        if (character_name_.empty()) return {};
        std::vector<std::uint8_t> reply{0x1A, 0x36, 0x00, 0x00, static_cast<std::uint8_t>(character_name_.size())};
        reply.insert(reply.end(), character_name_.begin(), character_name_.end());
        reply.push_back(0x01);
        return reply;
    }
    if (!tutorial_started_ && !created_template_.empty()
        && sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::character_create)) {
        if (auto name = request_name(client_body); !name.empty()) character_name_ = std::move(name);
        if (character_name_.empty()) return {};
        return sema::rename_character(created_template_, template_name_, character_name_);
    }
    if (!tutorial_started_ && !created_template_.empty()
        && sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::enter_world)) {
        entering_world_ = true;
        world_enter_requested_ = true;
        return {};
    }
    if (entering_world_ && sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::client_runtime_identity)) {
        entering_world_ = false;
        world_ready_requested_ = true;
        return {};
    }
    if (sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::target_lock_request)) {
        return sema::encode_gameplay_target_selected(sema::parse_gameplay_target_request(client_body));
    }
    if (sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::target_select_request)) {
        return sema::encode_gameplay_target_state(sema::parse_gameplay_target_request(client_body));
    }
    if (sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::initialize_tutorial)) {
        tutorial_requested_ = true;
        tutorial_started_ = true;
    } else if (tutorial_started_
        && sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::tutorial_objective_progress)) {
        tutorial_interactions_.push_back(sema::parse_gameplay_interaction(client_body));
        tutorial_movement_prompted_ = true;
    } else if (tutorial_movement_prompted_
        && (sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::movement_started)
            || sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::movement_updated)
            || sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::dash_path_update)
            || sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::movement_snapshot))) {
        const auto movement = sema::parse_gameplay_movement(client_body);
        player_position_ = movement;
        if (!tutorial_light_started_ && movement.x >= tutorial_cutscene_x) {
            tutorial_light_triggered_ = true;
            tutorial_light_started_ = true;
        }
    } else if (tutorial_light_started_ && !tutorial_monsters_spawned_
        && sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::cutscene_finished)) {
        // Seq_1278 places the player at this fixed encounter position. Movement
        // packets received afterward keep it current for combat validation.
        player_position_ = sema::gameplay_movement{-94161.3125F, 54676.16015625F, 23677.0F};
        tutorial_cutscene_finished_ = true;
        tutorial_monsters_spawned_ = true;
    } else if (tutorial_started_
        && sema::has_gameplay_opcode(client_body, sema::gameplay_client_message::cast)) {
        const auto cast = sema::parse_gameplay_client_cast(client_body);
        if (cast.skill_id != 4001) sprint_interrupted_ = true;  // any skill but the dash ends a sprint
        if (cast.skill_id == potion_item_skill) {
            // Assumed request; the official item-use request is protected and no
            // local session has used the potion yet.
            tutorial_potion_requested_ = tutorial_cast_action{cast, allocate_action_sequence()};
            return {};
        }
        const auto* skill = skills_.find(cast.skill_id);
        if (!skill) return {};
        if (skill->kind == sema::gameplay_skill_kind::dash) {
            // The phase is input mode (01 normally, A1 while an attack is held); a dash with any
            // phase must be answered or the client waits for it forever.
            tutorial_dash_requested_ = tutorial_cast_action{cast, allocate_action_sequence()};
        } else if (skill->kind != sema::gameplay_skill_kind::dash) {
            // Observed attack phases include 01, A1, A5, E1, and E5. The
            // phase is input mode, not authorization to use the skill.
            const bool accept = true;
            if (accept) {
                tutorial_attack_requested_ = tutorial_cast_action{cast, allocate_action_sequence()};
            }
        }
    }
    return {};
}
