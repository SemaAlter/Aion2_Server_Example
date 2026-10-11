#pragma once

#include <cstdint>
#include <span>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sema/gameplay.hpp"
#include "skill_catalog.hpp"

struct tutorial_cast_action {
    sema::gameplay_client_cast cast;
    std::uint8_t action_sequence;
};

class gameplay_session {
public:
    gameplay_session(std::uint16_t server_id, std::vector<std::uint8_t> session_data, const skill_catalog& skills);
    [[nodiscard]] std::vector<std::uint8_t> respond(std::span<const std::uint8_t> client_body);
    [[nodiscard]] std::size_t sequence() const noexcept { return sequence_; }
    [[nodiscard]] bool authenticated() const noexcept { return authenticated_; }
    [[nodiscard]] bool close_requested() const noexcept { return close_requested_; }
    [[nodiscard]] bool consume_tutorial_request() noexcept;
    // Character creation: the official 1C 36 record whose name is replaced by the chosen one.
    void set_character_template(std::vector<std::uint8_t> created_record, std::string template_name) {
        created_template_ = std::move(created_record);
        template_name_ = std::move(template_name);
    }
    [[nodiscard]] const std::string& character_name() const noexcept { return character_name_; }
    // True once after the enter-world request, and once after the client's 22 36 while entering.
    [[nodiscard]] bool consume_world_enter() noexcept { return std::exchange(world_enter_requested_, false); }
    [[nodiscard]] bool consume_world_ready() noexcept { return std::exchange(world_ready_requested_, false); }
    // Character entries the 15 36 login result lists.
    void set_login_characters(std::vector<std::vector<std::uint8_t>> characters) {
        login_characters_ = std::move(characters);
    }
    // Client 36 36 interactions (checkpoints and objects) received since the last call.
    [[nodiscard]] std::vector<sema::gameplay_interaction> consume_tutorial_interactions();
    [[nodiscard]] bool consume_tutorial_light_trigger() noexcept;
    [[nodiscard]] bool consume_tutorial_cutscene_finished() noexcept;
    [[nodiscard]] std::optional<tutorial_cast_action> consume_tutorial_dash() noexcept;
    [[nodiscard]] std::optional<tutorial_cast_action> consume_tutorial_attack() noexcept;
    // True once per landing: airborne movement (02 37 / 03 37) followed by ground movement or 05 37.
    [[nodiscard]] bool consume_tutorial_landing() noexcept;
    // True once after the client stops (04 37 / 18 37 snapshot), attacks, drinks, or interacts;
    // any of these ends a sprint.
    [[nodiscard]] bool consume_sprint_interrupted() noexcept;
    // True once per client 25 36 / 17 36 inside the tutorial (after completion, or leaving early).
    [[nodiscard]] bool consume_tutorial_leave_request() noexcept;
    // True once per takeoff (first airborne movement after ground movement).
    [[nodiscard]] bool consume_tutorial_takeoff() noexcept;
    // Client cast of the Life Potion item skill.
    [[nodiscard]] std::optional<tutorial_cast_action> consume_tutorial_potion() noexcept;
    static constexpr std::uint32_t potion_item_skill = 2011101;
    [[nodiscard]] std::optional<sema::gameplay_movement> player_position() const noexcept {
        return player_position_;
    }

private:
    std::uint16_t server_id_;
    std::vector<std::uint8_t> session_data_;
    const skill_catalog& skills_;
    std::size_t sequence_ = 0;
    bool authenticated_ = false;
    bool close_requested_ = false;
    bool tutorial_requested_ = false;
    bool tutorial_started_ = false;
    bool tutorial_movement_prompted_ = false;
    bool tutorial_light_triggered_ = false;
    bool tutorial_light_started_ = false;
    bool tutorial_cutscene_finished_ = false;
    bool tutorial_monsters_spawned_ = false;
    std::optional<sema::gameplay_movement> player_position_;
    std::uint8_t next_action_sequence_ = 1;
    std::optional<tutorial_cast_action> tutorial_dash_requested_;
    std::optional<tutorial_cast_action> tutorial_attack_requested_;
    std::optional<tutorial_cast_action> tutorial_potion_requested_;
    bool airborne_ = false;
    bool landed_ = false;
    bool took_off_ = false;
    bool tutorial_leave_requested_ = false;
    bool sprint_interrupted_ = false;
    std::vector<sema::gameplay_interaction> tutorial_interactions_;
    std::vector<std::vector<std::uint8_t>> login_characters_;
    std::vector<std::uint8_t> created_template_;
    std::string template_name_;
    std::string character_name_;
    bool entering_world_ = false;
    bool world_enter_requested_ = false;
    bool world_ready_requested_ = false;

    [[nodiscard]] std::uint8_t allocate_action_sequence() noexcept;
};
