#include "tutorial_world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace {

struct group_slot {
    std::uint32_t spawn_id;
    std::uint8_t group;
    std::uint32_t idle_social_action = 0;  // Common_Monster_Sidle (SocialAction 20000-20003), official 08 95
};

// Group order follows the official spawn order; the encounter quest steps flag monsters by it.
// Spawns that are not listed belong to group 1 after the listed ones.
constexpr std::array group_slots{
    group_slot{42773, 1}, group_slot{31645, 1}, group_slot{27516, 1}, group_slot{50958, 1},
    group_slot{55173, 2, 20003}, group_slot{59535, 2, 20001}, group_slot{37205, 2, 20000}, group_slot{41778, 2},
    group_slot{45702, 2, 20003},
    // Group 3 idles (official-tutorial-full 212.7 s) until checkpoint H engages it.
    group_slot{41456, 3, 20003}, group_slot{46192, 3, 20002}, group_slot{41228, 3, 20001},
    group_slot{58382, 3, 20001}, group_slot{34583, 3}, group_slot{41927, 3, 20004},
    // Group 4: the Balaur released from the second Illuminated Test Tube.
    group_slot{52705, 4},
};

struct group_area {
    std::uint8_t group;
    float min_x, max_x, min_y, max_y;
};

// Official monster positions plus a margin (group 1: x -94823..-92385, y 54421..55040;
// group 2: x -78819..-78359, y 58220..60565). The server has no navigation mesh, so monsters
// stay inside the area their encounter used instead of walking through walls after the player.
constexpr std::array group_areas{
    group_area{1, -95100.0F, -92100.0F, 54300.0F, 55150.0F},
    group_area{2, -79400.0F, -77800.0F, 56500.0F, 61000.0F},
    group_area{3, -72700.0F, -70200.0F, 63000.0F, 66200.0F},  // test-tube room
    group_area{4, -72700.0F, -70200.0F, 63000.0F, 66200.0F},
};

} // namespace

tutorial_world::tutorial_world(
    const sema::data::game_data_store& data,
    const sema::data::map_id map) {
    for (const auto& [id, spawn] : data.spawns.records()) {
        if (spawn.kind != sema::data::spawn_kind::monster || spawn.map != map) continue;
        const auto* monster = data.monsters.find(spawn.template_ref);
        if (!monster) throw std::runtime_error("tutorial spawn references an unknown monster");
        if (monster->max_health > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("tutorial entity fields exceed the gameplay protocol");
        }
        monster_state state{id, monster->npc_id, spawn.position, spawn.heading,
            static_cast<std::uint32_t>(monster->max_health), static_cast<std::uint32_t>(monster->max_health),
            monster->attack, monster->move_speed, monster->attack_range,
            monster->server_skill_id, monster->attack_interval_ms};
        const auto slot = std::ranges::find(group_slots, id, &group_slot::spawn_id);
        if (slot != group_slots.end()) {
            state.group = slot->group;
            state.idle_social_action = slot->idle_social_action;
        }
        const auto area = std::ranges::find(group_areas, state.group, &group_area::group);
        state.spawn_in_area = area != group_areas.end() && spawn.position.x >= area->min_x
            && spawn.position.x <= area->max_x && spawn.position.y >= area->min_y && spawn.position.y <= area->max_y;
        monsters_.push_back(state);
    }
    const auto order = [](const monster_state& monster) {
        const auto slot = std::ranges::find(group_slots, monster.entity_id, &group_slot::spawn_id);
        return std::pair{static_cast<std::size_t>(slot - group_slots.begin()), monster.entity_id};
    };
    std::ranges::sort(monsters_, {}, order);
    std::array<std::size_t, 256> next_index{};
    for (auto& monster : monsters_) monster.index_in_group = next_index[monster.group]++;
}

std::vector<sema::gameplay_timed_message> tutorial_world::spawn_group(
    const std::uint8_t group,
    const std::uint32_t player_entity_id,
    const bool engage,
    const bool passive) {
    std::lock_guard lock(mutex_);
    std::vector<sema::gameplay_timed_message> result;
    for (auto& monster : monsters_) {
        if (monster.group != group || monster.spawned) continue;
        monster.spawned = true;
        monster.passive = passive;
        result.push_back({result.empty() ? 0U : 50U,
            sema::encode_gameplay_monster_spawn(monster.entity_id, monster.template_id,
                monster.position.x, monster.position.y, monster.position.z, monster.heading, monster.max_health)});
    }
    if (engage && !passive) {
        for (auto& monster : monsters_) {
            if (monster.group != group || monster.engaged) continue;
            monster.engaged = true;
            for (auto& message : sema::gameplay_monster_activation_sequence(monster.entity_id, player_entity_id)) {
                result.push_back(std::move(message));
            }
        }
    }
    return result;
}

void tutorial_world::hold(const std::uint32_t entity_id, const std::uint64_t milliseconds) noexcept {
    std::lock_guard lock(mutex_);
    for (auto& monster : monsters_) {
        if (monster.entity_id == entity_id) monster.busy_until_ms = clock_ms_ + milliseconds;
    }
}

void tutorial_world::engage_group(const std::uint8_t group) noexcept {
    std::lock_guard lock(mutex_);
    for (auto& monster : monsters_) {
        if (monster.group != group || !monster.spawned || monster.health == 0) continue;
        monster.passive = false;
        if (!monster.engaged) {
            monster.engaged = true;
            monster.activation_pending = true;
        }
    }
}

void tutorial_world::update_player_position(const sema::gameplay_movement position) noexcept {
    std::lock_guard lock(mutex_);
    player_position_ = position;
    has_player_position_ = true;
}

std::vector<sema::gameplay_attack_target> tutorial_world::apply_attack(
    const sema::gameplay_skill_kind kind,
    const sema::data::skill_hit_shape hit_shape,
    const std::uint16_t max_targets,
    const std::uint32_t requested_target,
    const sema::gameplay_movement player_position,
    const float range_world_units,
    const float player_heading_degrees,
    const float angle_degrees) {
    std::lock_guard lock(mutex_);
    std::vector<monster_state*> selected;
    if (range_world_units <= 0.0F) return {};
    const auto attackable = [](const monster_state& monster) {
        return monster.spawned && !monster.passive && monster.health != 0;
    };
    const auto in_range = [&](const monster_state& monster) {
        const auto dx = monster.position.x - player_position.x;
        const auto dy = monster.position.y - player_position.y;
        const auto distance_squared = dx * dx + dy * dy;
        if (distance_squared > range_world_units * range_world_units) return false;
        if (hit_shape != sema::data::skill_hit_shape::frontal_cone || distance_squared <= 0.001F) return true;
        const auto heading_radians = player_heading_degrees * std::numbers::pi_v<float> / 180.0F;
        const auto distance = std::sqrt(distance_squared);
        const auto facing_dot = (dx * std::cos(heading_radians) + dy * std::sin(heading_radians)) / distance;
        const auto half_angle_radians = angle_degrees * std::numbers::pi_v<float> / 360.0F;
        return facing_dot >= std::cos(half_angle_radians);
    };
    if (hit_shape == sema::data::skill_hit_shape::radius
        || hit_shape == sema::data::skill_hit_shape::frontal_cone) {
        for (auto& monster : monsters_) {
            if (attackable(monster) && in_range(monster)) selected.push_back(&monster);
        }
        std::ranges::sort(selected, [&](const monster_state* left, const monster_state* right) {
            const auto left_x = left->position.x - player_position.x;
            const auto left_y = left->position.y - player_position.y;
            const auto right_x = right->position.x - player_position.x;
            const auto right_y = right->position.y - player_position.y;
            return left_x * left_x + left_y * left_y < right_x * right_x + right_y * right_y;
        });
        if (max_targets != 0 && selected.size() > max_targets) selected.resize(max_targets);
    } else {
        const auto requested = std::ranges::find(monsters_, requested_target, &monster_state::entity_id);
        if (requested != monsters_.end() && attackable(*requested) && in_range(*requested)) {
            selected.push_back(&*requested);
        } else {
            const auto nearest = std::ranges::min_element(monsters_, {}, [&](const monster_state& monster) {
                const auto dx = monster.position.x - player_position.x;
                const auto dy = monster.position.y - player_position.y;
                return attackable(monster) ? dx * dx + dy * dy : std::numeric_limits<float>::max();
            });
            if (nearest != monsters_.end() && attackable(*nearest) && in_range(*nearest)) {
                selected.push_back(&*nearest);
            }
        }
    }

    const std::uint32_t damage = kind == sema::gameplay_skill_kind::secondary_attack ? 9U : 8U;
    std::vector<sema::gameplay_attack_target> result;
    for (auto* monster : selected) {
        if (!monster->engaged) {
            monster->engaged = true;
            monster->activation_pending = true;
        }
        const auto applied = std::min(damage, monster->health);
        monster->health -= applied;
        result.push_back({monster->entity_id, monster->health, static_cast<std::uint16_t>(applied),
            monster->health == 0});
    }
    return result;
}

std::vector<sema::gameplay_timed_message> tutorial_world::tick(
    const std::uint32_t player_entity_id,
    const std::uint32_t step_milliseconds) {
    std::lock_guard lock(mutex_);
    std::vector<sema::gameplay_timed_message> result;
    if (player_health_ == 0 || !has_player_position_) return result;
    clock_ms_ += step_milliseconds;

    if (sprinting_) {
        player_sp_ -= std::min(500U, player_sp_);
        result.push_back({0, sema::encode_gameplay_entity_resource(player_entity_id, 0x03, player_sp_)});
        if (player_sp_ == 0) {
            sprinting_ = false;
            for (auto& body : sema::encode_gameplay_sprint_end(player_entity_id)) result.push_back({0, std::move(body)});
        }
        next_regen_ms_ = clock_ms_ + 300;
    } else if (clock_ms_ >= next_regen_ms_) {
        next_regen_ms_ = clock_ms_ + 1'000;
        const bool battle = std::ranges::any_of(monsters_, [](const monster_state& monster) {
            return monster.engaged && monster.health != 0 && !monster.passive;
        });
        const auto sp = std::min(player_max_sp_, player_sp_ + (battle ? 5'000U : 20'000U));
        const auto mp = std::min(player_max_mp_, player_mp_ + (battle ? 25U : 250U));
        const auto hp = std::min<std::uint64_t>(player_max_health_, player_health_ + (battle ? 5U : 10U));
        if (sp != player_sp_) result.push_back({0, sema::encode_gameplay_entity_resource(player_entity_id, 0x03, sp)});
        if (mp != player_mp_) result.push_back({0, sema::encode_gameplay_entity_resource(player_entity_id, 0x01, mp)});
        if (hp != player_health_) result.push_back({0, sema::encode_gameplay_entity_health(player_entity_id, hp)});
        player_sp_ = sp;
        player_mp_ = mp;
        player_health_ = hp;
    }

    if (heal_ticks_left_ != 0 && clock_ms_ >= heal_next_ms_) {
        --heal_ticks_left_;
        heal_remaining_ = static_cast<std::uint8_t>(heal_remaining_ - 9);
        heal_next_ms_ = clock_ms_ + 2'000;
        player_health_ = std::min<std::uint64_t>(player_health_ + 10, player_max_health_);
        for (auto& body : sema::encode_gameplay_potion_tick(player_entity_id, heal_skill_, heal_remaining_, heal_ticks_left_)) {
            result.push_back({0, std::move(body)});
        }
        result.push_back({0, sema::encode_gameplay_entity_health(player_entity_id, player_health_)});
        if (heal_ticks_left_ == 0) {
            result.push_back({300, sema::encode_gameplay_action_finished(player_entity_id, heal_state_, 0x01)});
        }
    }

    // A pack fights together: one engaged member (seen or hit) pulls in the rest of its group.
    for (const auto& leader : monsters_) {
        if (!leader.engaged || !leader.spawned || leader.health == 0) continue;
        for (auto& mate : monsters_) {
            if (mate.group != leader.group || mate.engaged || !mate.spawned || mate.passive || mate.health == 0) continue;
            mate.engaged = true;
            mate.activation_pending = true;
        }
    }
    for (auto& monster : monsters_) {
        if (!monster.spawned || monster.passive || monster.health == 0) {
            monster.moving = false;
            continue;
        }
        const auto dx = player_position_.x - monster.position.x;
        const auto dy = player_position_.y - monster.position.y;
        const auto distance = std::hypot(dx, dy);
        if (!monster.engaged && distance <= engage_distance) {
            monster.engaged = true;
            monster.activation_pending = true;
        }
        if (!monster.engaged) continue;
        if (monster.activation_pending) {
            monster.activation_pending = false;
            if (monster.idle_social_action != 0) {
                result.push_back({0, sema::encode_gameplay_social_action(monster.entity_id, monster.idle_social_action, false)});
            }
            std::uint32_t offset = 0;
            for (auto& message : sema::gameplay_monster_activation_sequence(monster.entity_id, player_entity_id)) {
                offset += message.delay_milliseconds;
                result.push_back({offset, std::move(message.body)});
            }
        }
        if (clock_ms_ < monster.busy_until_ms) continue;
        const auto attack_reach = monster.attack_range * 100.0F;
        // Chase until comfortably inside the reach so small player steps do not restart
        // the walk on every tick.
        const auto stop_distance = attack_reach * 0.8F;
        const sema::gameplay_movement position{monster.position.x, monster.position.y, monster.position.z};
        if (distance > 0.001F) monster.heading = std::atan2(dy, dx) * 180.0F / std::numbers::pi_v<float>;

        if (distance > (monster.moving ? stop_distance : attack_reach)) {
            if (!monster.moving) {
                result.push_back({0, sema::encode_gameplay_monster_movement_started(
                    monster.entity_id, position, monster.heading)});
                monster.moving = true;
            }
            const auto step = std::min(monster.move_speed * 100.0F * static_cast<float>(step_milliseconds) / 1000.0F,
                distance - stop_distance);
            auto next_x = monster.position.x + dx / distance * step;
            auto next_y = monster.position.y + dy / distance * step;
            const auto area = std::ranges::find(group_areas, monster.group, &group_area::group);
            if (area != group_areas.end() && monster.spawn_in_area) {
                next_x = std::clamp(next_x, area->min_x, area->max_x);
                next_y = std::clamp(next_y, area->min_y, area->max_y);
            }
            const auto moved_x = next_x - monster.position.x;
            const auto moved_y = next_y - monster.position.y;
            const auto moved = std::hypot(moved_x, moved_y);
            if (moved < 1.0F) {
                // At the edge of its area: wait there instead of walking in place.
                if (monster.moving) {
                    result.push_back({0, sema::encode_gameplay_monster_movement_stopped(
                        monster.entity_id, position, monster.heading)});
                    monster.moving = false;
                }
                continue;
            }
            monster.position.x = next_x;
            monster.position.y = next_y;
            result.push_back({0, sema::encode_gameplay_monster_movement_updated(
                monster.entity_id, moved_x / moved, moved_y / moved, monster.heading)});
            continue;
        }
        if (monster.moving) {
            result.push_back({0, sema::encode_gameplay_monster_movement_stopped(
                monster.entity_id, position, monster.heading)});
            monster.moving = false;
        }
        if (clock_ms_ < monster.next_attack_ms) continue;

        const auto damage = static_cast<std::uint8_t>(std::clamp<std::uint32_t>(monster.attack, 1, 255));
        // No death or resurrection flow is captured, so tutorial health stops at 1.
        player_health_ -= std::min<std::uint64_t>(damage, player_health_ - 1);
        // Casters (attack range beyond melee, e.g. the 20 m Drakan) fire projectiles.
        auto attack = monster.attack_range > 5.0F
            ? sema::gameplay_monster_ranged_attack_sequence(monster.entity_id, player_entity_id,
                monster.server_skill_id, next_monster_action_sequence_, player_position_, monster.heading,
                damage, player_health_)
            : sema::gameplay_monster_attack_sequence(monster.entity_id, player_entity_id,
                monster.server_skill_id, next_monster_action_sequence_, player_position_, monster.heading,
                damage, player_health_);
        std::uint32_t offset = 0;
        for (auto& message : attack) {
            offset += message.delay_milliseconds;
            result.push_back({offset, std::move(message.body)});
        }
        monster.busy_until_ms = clock_ms_ + offset;
        monster.next_attack_ms = clock_ms_ + monster.attack_interval_ms;
        if (++next_monster_action_sequence_ == 0) next_monster_action_sequence_ = 1;
    }
    return result;
}

bool tutorial_world::try_use_skill(
    const std::uint32_t skill_id,
    const std::uint32_t cooldown_milliseconds,
    const std::chrono::steady_clock::time_point now) {
    std::lock_guard lock(mutex_);
    const auto ready = skill_ready_at_.find(skill_id);
    if (ready != skill_ready_at_.end() && now + cooldown_latency_allowance < ready->second) return false;
    skill_ready_at_[skill_id] = now + std::chrono::milliseconds(cooldown_milliseconds);
    return true;
}

void tutorial_world::start_heal_over_time(const std::uint32_t item_skill_id, const std::uint8_t action_state) noexcept {
    std::lock_guard lock(mutex_);
    heal_skill_ = item_skill_id;
    heal_state_ = action_state;
    heal_ticks_left_ = 5;
    heal_remaining_ = 0x30;
    heal_next_ms_ = clock_ms_ + 2'000;
}

std::vector<sema::gameplay_timed_message> tutorial_world::spend_resources(
    const std::uint32_t player_entity_id, const std::uint32_t mp_cost, const std::uint32_t sp_cost) {
    std::lock_guard lock(mutex_);
    std::vector<sema::gameplay_timed_message> result;
    if (mp_cost != 0) {
        player_mp_ -= std::min(mp_cost, player_mp_);
        result.push_back({0, sema::encode_gameplay_entity_resource(player_entity_id, 0x01, player_mp_)});
    }
    if (sp_cost != 0) {
        player_sp_ -= std::min(sp_cost, player_sp_);
        result.push_back({0, sema::encode_gameplay_entity_resource(player_entity_id, 0x03, player_sp_)});
    }
    if (!result.empty()) next_regen_ms_ = clock_ms_ + 1'000;
    return result;
}

void tutorial_world::start_sprint() noexcept {
    std::lock_guard lock(mutex_);
    sprinting_ = player_sp_ != 0;
}

bool tutorial_world::stop_sprint() noexcept {
    std::lock_guard lock(mutex_);
    // Official regeneration resumes about 0.3 s after the sprint ends.
    if (sprinting_) next_regen_ms_ = clock_ms_ + 300;
    return std::exchange(sprinting_, false);
}

std::uint32_t tutorial_world::player_mp() const noexcept {
    std::lock_guard lock(mutex_);
    return player_mp_;
}

std::uint32_t tutorial_world::player_sp() const noexcept {
    std::lock_guard lock(mutex_);
    return player_sp_;
}

std::uint64_t tutorial_world::player_health() const noexcept {
    std::lock_guard lock(mutex_);
    return player_health_;
}

std::uint8_t tutorial_world::claim_player_state(const std::uint8_t advance) noexcept {
    std::lock_guard lock(mutex_);
    const auto state = next_player_state_;
    next_player_state_ = static_cast<std::uint8_t>(next_player_state_ + advance);
    return state;
}

bool tutorial_world::group_cleared(const std::uint8_t group) const noexcept {
    std::lock_guard lock(mutex_);
    bool any = false;
    for (const auto& monster : monsters_) {
        if (monster.group != group) continue;
        any = true;
        if (!monster.spawned || monster.health != 0) return false;
    }
    return any;
}

bool tutorial_world::is_dead(const std::uint32_t entity_id) const noexcept {
    std::lock_guard lock(mutex_);
    const auto found = std::ranges::find(monsters_, entity_id, &monster_state::entity_id);
    return found != monsters_.end() && found->health == 0;
}

std::optional<tutorial_world::monster_info> tutorial_world::monster(const std::uint32_t entity_id) const noexcept {
    std::lock_guard lock(mutex_);
    const auto found = std::ranges::find(monsters_, entity_id, &monster_state::entity_id);
    if (found == monsters_.end()) return std::nullopt;
    return monster_info{found->template_id, found->group, found->index_in_group, found->position};
}

std::vector<std::uint32_t> tutorial_world::group_members(const std::uint8_t group) const {
    std::lock_guard lock(mutex_);
    std::vector<std::uint32_t> result;
    for (const auto& monster : monsters_) {
        if (monster.group == group) result.push_back(monster.entity_id);
    }
    return result;
}
