#pragma once

#include "types.hpp"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace sema::data {

struct loot_entry {
    template_id item_id{};
    std::uint32_t minimum_count{1};
    std::uint32_t maximum_count{1};
    float chance{1.0F};
};

enum class loot_link_source : std::uint8_t { package, decompose, level_scale };

struct loot_link {
    template_id child_table{};
    float chance{1.0F};
    std::uint32_t count{1};
    std::uint16_t level_min{};  // 0 means "any level"
    std::uint16_t level_max{};
    loot_link_source source{loot_link_source::package};
};

struct loot_table {
    template_id id{};
    std::string name;
    std::vector<loot_entry> entries;
    std::vector<loot_link> links;
};

enum class loot_owner_kind : std::uint8_t { npc, env_object, dungeon, item, quest, field_event, other };

struct loot_drop {
    template_id item_id{};
    std::uint32_t count{};
};

struct game_data_store;

// Rolls a loot table: each entry and child link is an independent chance roll, level-scaled
// links only apply inside their level range, and child tables are rolled recursively.
// Independent per-entry rolls; switch to weighted picks if captures show the client expects them.
[[nodiscard]] std::vector<loot_drop> roll_loot(
    const game_data_store& data, template_id table, std::uint16_t level, std::mt19937& random);

} // namespace sema::data
