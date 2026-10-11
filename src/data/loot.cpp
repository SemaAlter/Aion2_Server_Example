#include "sema/data/game_data.hpp"

#include <algorithm>

namespace sema::data {
namespace {

constexpr int maximum_depth = 16;  // loot graphs are acyclic in the client; this guards bad overrides

void roll(const game_data_store& data, const template_id table_id, const std::uint16_t level, std::mt19937& random,
    const int depth, std::vector<loot_drop>& drops) {
    const auto* table = data.loot_tables.find(table_id);
    if (!table || depth > maximum_depth) return;
    std::uniform_real_distribution<float> chance(0.0F, 1.0F);
    for (const auto& entry : table->entries) {
        if (chance(random) >= entry.chance) continue;
        std::uniform_int_distribution<std::uint32_t> count(entry.minimum_count, (std::max)(entry.minimum_count, entry.maximum_count));
        if (const auto amount = count(random); amount > 0) drops.push_back({entry.item_id, amount});
    }
    for (const auto& link : table->links) {
        if (link.level_max != 0 && (level < link.level_min || level > link.level_max)) continue;
        if (chance(random) >= link.chance) continue;
        for (std::uint32_t i = 0; i < (std::max)(1U, link.count); ++i) roll(data, link.child_table, level, random, depth + 1, drops);
    }
}

} // namespace

std::vector<loot_drop> roll_loot(
    const game_data_store& data, const template_id table, const std::uint16_t level, std::mt19937& random) {
    std::vector<loot_drop> drops;
    roll(data, table, level, random, 0, drops);
    return drops;
}

} // namespace sema::data
