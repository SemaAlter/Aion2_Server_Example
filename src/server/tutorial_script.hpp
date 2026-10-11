#pragma once

#include "replay_scenario.hpp"
#include "tutorial_world.hpp"
#include "sema/gameplay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Server side of the Elyos tutorial instance after the bootstrap replay, following the official
// run (official-tutorial-combat-door): checkpoint interactions, quest steps, prompts, monster
// groups, kill counters, the test-tube room, and the instance-layer exit.
class tutorial_script {
public:
    struct environment_object {
        std::uint32_t entity_id{};
        std::uint32_t template_id{};
        float x{};
        float y{};
    };

    tutorial_script() = default;
    // Reads the quest instance and environment objects the bootstrap replay creates.
    tutorial_script(const replay_scenario& bootstrap,
        std::optional<replay_phase> room_spawns,
        std::optional<replay_phase> exit);

    [[nodiscard]] std::uint32_t quest_instance() const noexcept { return quest_instance_; }
    // Character token ("$" + 11 characters) of the bootstrap's 33 36 character record.
    [[nodiscard]] const std::string& character_token() const noexcept { return character_token_; }
    // Uses one Life Potion from the bootstrap inventory; returns the item with its new count.
    [[nodiscard]] std::optional<sema::gameplay_inventory_item> use_potion() noexcept;
    // Replaces character tokens recorded in another session with ours.
    void retoken(std::vector<std::uint8_t>& body) const;
    [[nodiscard]] const std::vector<environment_object>& objects() const noexcept { return objects_; }
    // The cutscene handoff was recorded in another session: give its quest steps our quest
    // instance, its light-checkpoint interaction (its first 38 36) our object, and its
    // character record (33 36) ours, and its cutscene action (2A 38 / 2B 38) the action
    // sequence the live cutscene state (2C 38) uses.
    void adapt_handoff(std::vector<replay_event>& events, std::uint8_t cutscene_action) const;
    // Client 36 36. Returns the official responses for the checkpoint or door, if any.
    [[nodiscard]] std::vector<sema::gameplay_timed_message> on_interaction(
        const sema::gameplay_interaction& interaction, std::uint32_t player_entity_id,
        tutorial_world& world, std::uint64_t server_time_milliseconds);
    // Player attacks also break the test tubes in the room (official-tutorial-full 222-232 s):
    // tubes inside the skill's range and cone are destroyed, the Illuminated ones release an NPC or
    // a Balaur and advance quest step 3/2.
    [[nodiscard]] std::vector<sema::gameplay_timed_message> on_attack(
        sema::gameplay_movement position, float facing_degrees, float range_world_units, float angle_degrees,
        bool frontal, std::uint32_t player_entity_id, tutorial_world& world, std::uint64_t server_time_milliseconds);
    // Client 25 36 in the tutorial: the exit (47 36 0A 00, the 26 36 roster); the client then plays
    // the end cutscene and closes for character creation. Once per session.
    [[nodiscard]] std::vector<sema::gameplay_timed_message> leave(
        std::uint32_t player_entity_id, std::uint64_t server_time_milliseconds);
    // The same exit when the client has not asked within leave_fallback_milliseconds of completion;
    // a later 17 36 is still answered once.
    [[nodiscard]] std::vector<sema::gameplay_timed_message> leave_if_overdue(
        std::uint32_t player_entity_id, std::uint64_t server_time_milliseconds);
    // The client sends 25 36 ~30 ms after the tutorial-complete 01 61 / 00 61; official answers 0.12 s later.
    static constexpr std::uint64_t leave_fallback_milliseconds = 2'000;
    static constexpr std::uint32_t exit_answer_delay_milliseconds = 120;
    // Kill counters and encounter quest steps for the targets a player skill just killed.
    [[nodiscard]] std::vector<sema::gameplay_timed_message> on_kills(
        std::span<const sema::gameplay_attack_target> targets, std::uint32_t skill_id,
        std::uint32_t player_entity_id, tutorial_world& world, std::uint64_t server_time_milliseconds);

    // Checkpoints in the official order. Positions identify the bootstrap's Arrival Point objects.
    // room_a / room_b are the room's last two Arrival Points; the first reached after the finale
    // is checkpoint H (group 3 engages), the next one after quest step 3/3 begins ends the tutorial.
    enum class checkpoint : std::uint8_t {
        movement, light, corridor, corridor_end, encounter, room, finale, room_a, room_b };
    [[nodiscard]] std::optional<checkpoint> checkpoint_of(std::uint32_t object_entity_id) const noexcept;

    static constexpr std::uint32_t arrival_point_template = 1100001;
    static constexpr std::uint32_t castle_door_template = 1400218;
    static constexpr std::uint32_t blocked_door_template = 1400217;
    static constexpr std::uint32_t exit_delay_milliseconds = 8'500;
    static constexpr std::uint32_t life_potion_template = 530126003;
    static constexpr std::uint32_t darkened_tube_template = 1400223;
    static constexpr std::uint32_t captive_tube_template = 1400225;  // releases the NPC
    static constexpr std::uint32_t balaur_tube_template = 1400224;   // releases the Balaur (group 4)
    static constexpr std::uint32_t boss_emerge_action = 9'999'506;
    static constexpr std::uint32_t boss_emerge_milliseconds = 4'933;

private:
    std::uint32_t quest_instance_ = 0;
    std::string character_token_;
    std::vector<std::uint8_t> character_record_;  // the bootstrap's 33 36
    std::optional<sema::gameplay_inventory_item> potion_;
    std::vector<environment_object> objects_;
    [[nodiscard]] std::vector<sema::gameplay_timed_message> replay(
        const replay_phase& phase, std::uint32_t player_entity_id, std::uint64_t now) const;
    [[nodiscard]] std::vector<sema::gameplay_timed_message> release_boss(
        std::uint32_t player_entity_id, tutorial_world& world, std::uint64_t now);

    std::optional<replay_phase> room_spawns_;
    std::optional<replay_phase> exit_;
    std::array<bool, 9> checkpoint_done_{};
    struct tube {
        std::uint32_t entity_id;
        std::uint32_t template_id;
        float x;
        float y;
        bool broken = false;
    };
    std::vector<tube> tubes_;
    std::vector<std::uint32_t> room_objects_;  // every object the room-spawns phase creates
    std::vector<std::uint8_t> room_flags_ = std::vector<std::uint8_t>(6, 0);  // quest 3/1
    std::vector<std::uint8_t> tube_flags_ = std::vector<std::uint8_t>(4, 0);  // quest 3/2
    bool checkpoint_h_done_ = false;
    bool tube_step_started_ = false;    // quest 3/2
    bool exit_step_started_ = false;    // quest 3/3
    bool tutorial_finished_ = false;
    bool boss_released_ = false;
    bool left_ = false;
    bool leave_answered_ = false;
    std::uint64_t finished_ms_ = 0;
    bool door_done_ = false;
    std::vector<std::uint8_t> encounter1_flags_ = std::vector<std::uint8_t>(4, 0);
    std::vector<std::uint8_t> encounter2_flags_ = std::vector<std::uint8_t>(3, 0);
    bool encounter1_completed_ = false;
    bool encounter2_completed_ = false;
    std::uint32_t kills_ = 0;
    std::uint32_t combo_kills_ = 0;
};
