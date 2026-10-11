#pragma once

#include "sema/data/game_data.hpp"
#include "sema/gameplay.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

// Monsters of the Elyos tutorial instance, in the spawn groups of the official run
// (official-tutorial-combat-door): group 1 is the post-cutscene encounter, group 2 the
// corridor encounter, and group 3 the passive speakers of the final dialogue.
class tutorial_world {
public:
    struct monster_info {
        std::uint32_t template_id{};
        std::uint8_t group{};
        std::size_t index_in_group{};
        sema::data::vector3 position;
    };

    tutorial_world() = default;
    tutorial_world(const sema::data::game_data_store& data, sema::data::map_id map);

    // Spawns every monster of the group. Hostile groups attack once engaged (immediately when
    // engage is true, otherwise when the player comes close or hits them); passive groups only stand.
    [[nodiscard]] std::vector<sema::gameplay_timed_message> spawn_group(
        std::uint8_t group, std::uint32_t player_entity_id, bool engage, bool passive);
    void update_player_position(sema::gameplay_movement position) noexcept;
    // Turns a spawned (possibly passive) group hostile; activation records follow in tick().
    void engage_group(std::uint8_t group) noexcept;
    // Keeps a monster still (no chase or attack) for a while, e.g. during its spawn animation.
    void hold(std::uint32_t entity_id, std::uint64_t milliseconds) noexcept;
    [[nodiscard]] std::vector<sema::gameplay_attack_target> apply_attack(
        sema::gameplay_skill_kind kind,
        sema::data::skill_hit_shape hit_shape,
        std::uint16_t max_targets,
        std::uint32_t requested_target,
        sema::gameplay_movement player_position,
        float range_world_units,
        float player_heading_degrees,
        float angle_degrees);
    // Advances monster AI by one step. Each returned delay is an offset from now, not from the
    // previous message, so callers can interleave several monsters' sequences.
    [[nodiscard]] std::vector<sema::gameplay_timed_message> tick(
        std::uint32_t player_entity_id, std::uint32_t step_milliseconds);
    // Starts the skill's cooldown and returns true when it is ready; casts that arrive early
    // (beyond a small latency allowance) are rejected.
    [[nodiscard]] bool try_use_skill(
        std::uint32_t skill_id, std::uint32_t cooldown_milliseconds, std::chrono::steady_clock::time_point now);
    // Life Potion heal-over-time: +10 health every 2 s for 10 s, then the action ends
    // (official-tutorial-combat-door 84.2-94.3 s).
    void start_heal_over_time(std::uint32_t item_skill_id, std::uint8_t action_state) noexcept;
    [[nodiscard]] std::uint64_t player_health() const noexcept;
    // Spends MP/SP for a player skill (Skill.NeedCostMp / NeedCostSp) and returns the resource
    // updates; regeneration follows in tick() at the tutorial character's rest/battle rates.
    [[nodiscard]] std::vector<sema::gameplay_timed_message> spend_resources(
        std::uint32_t player_entity_id, std::uint32_t mp_cost, std::uint32_t sp_cost);
    // A dash turns into a sprint that drains 500 SP per 100 ms until the player stops, attacks, or
    // interacts (official: 60.5-64.0 s, 184.2-193.4 s); regeneration waits until it ends.
    void start_sprint() noexcept;
    // Ends the sprint; returns true when one was running (the caller sends 22 37 / 42 37).
    [[nodiscard]] bool stop_sprint() noexcept;
    [[nodiscard]] std::uint32_t player_mp() const noexcept;
    [[nodiscard]] std::uint32_t player_sp() const noexcept;
    // Returns the next 2A 38 / 2C 38 action sequence and advances the counter by `advance`.
    [[nodiscard]] std::uint8_t claim_player_state(std::uint8_t advance) noexcept;
    [[nodiscard]] bool group_cleared(std::uint8_t group) const noexcept;
    [[nodiscard]] bool is_dead(std::uint32_t entity_id) const noexcept;
    [[nodiscard]] std::optional<monster_info> monster(std::uint32_t entity_id) const noexcept;
    // Entity IDs of the group's monsters, in group order.
    [[nodiscard]] std::vector<std::uint32_t> group_members(std::uint8_t group) const;
    [[nodiscard]] std::size_t monster_count() const noexcept { return monsters_.size(); }

    static constexpr std::uint32_t ai_step_milliseconds = 100;
    static constexpr auto cooldown_latency_allowance = std::chrono::milliseconds(100);
    static constexpr float engage_distance = 800.0F;  // world units (8 m)

private:
    struct monster_state {
        std::uint32_t entity_id{};
        std::uint32_t template_id{};
        sema::data::vector3 position;
        float heading{};
        std::uint32_t max_health{};
        std::uint32_t health{};
        std::uint32_t attack{};
        float move_speed{};
        float attack_range{};
        std::uint32_t server_skill_id{};
        std::uint32_t attack_interval_ms{};
        std::uint8_t group = 1;
        std::uint32_t idle_social_action = 0;  // ended when the monster engages
        bool spawn_in_area = false;            // movement is limited to the group's encounter area
        std::size_t index_in_group = 0;
        bool spawned = false;
        bool passive = false;
        bool engaged = false;
        bool activation_pending = false;
        bool moving = false;
        std::uint64_t busy_until_ms = 0;
        std::uint64_t next_attack_ms = 0;
    };

    mutable std::mutex mutex_;
    std::vector<monster_state> monsters_;
    static constexpr std::uint64_t player_max_health_ = 119;  // tutorial EStat HPMax
    std::uint64_t player_health_ = player_max_health_;
    sema::gameplay_movement player_position_{};
    bool has_player_position_ = false;
    std::uint64_t clock_ms_ = 0;
    std::uint8_t next_player_state_ = 0x08;  // the bootstrap replay already sent 05 and 07
    std::uint8_t next_monster_action_sequence_ = 1;
    std::uint32_t heal_skill_ = 0;
    std::uint8_t heal_state_ = 0;
    std::uint8_t heal_ticks_left_ = 0;
    // Tutorial character stats (49 36): MPMax 1500, SPMax 100000; regeneration per second
    // resting / in battle: SP 20000 / 5000, MP 250 / 25, HP 10 / 5.
    static constexpr std::uint32_t player_max_mp_ = 1'500;
    static constexpr std::uint32_t player_max_sp_ = 100'000;
    std::uint32_t player_mp_ = player_max_mp_;
    std::uint32_t player_sp_ = player_max_sp_;
    std::uint64_t next_regen_ms_ = 1'000;
    bool sprinting_ = false;
    std::uint8_t heal_remaining_ = 0;
    std::uint64_t heal_next_ms_ = 0;
    std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> skill_ready_at_;
};
