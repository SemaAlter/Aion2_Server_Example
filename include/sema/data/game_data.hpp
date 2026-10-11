#pragma once

#include "catalog.hpp"
#include "env_object.hpp"
#include "item.hpp"
#include "loot.hpp"
#include "monster.hpp"
#include "npc.hpp"
#include "quest.hpp"
#include "skill.hpp"
#include "spawn.hpp"
#include "walk_mesh.hpp"
#include "world_event.hpp"

#include <filesystem>
#include <cstddef>
#include <span>
#include <unordered_map>
#include <vector>

namespace sema::data {

struct game_data_store {
    catalog<skill_template> skills;
    catalog<item_template> items;
    catalog<npc_template> npcs;
    catalog<monster_template> monsters;
    catalog<spawn_point> spawns;
    catalog<quest_template> quests;
    catalog<world_event_template> events;
    catalog<map_info> maps;
    catalog<loot_table> loot_tables;
    catalog<env_object_template> env_objects;
    catalog<env_object_spawn> env_object_spawns;
    std::size_t indexed_client_tables{};
    std::unordered_map<map_id, walk_mesh> walk_meshes;

    void add_loot_owner(loot_owner_kind kind, template_id owner, template_id table);
    // Builds the per-map spawn and event indexes; call once after every catalog is loaded.
    void build_indexes();
    void validate() const;

    [[nodiscard]] std::span<const template_id> loot_tables_for(loot_owner_kind kind, template_id owner) const;
    [[nodiscard]] std::span<const template_id> spawns_on(map_id map) const;
    [[nodiscard]] std::span<const template_id> env_object_spawns_on(map_id map) const;
    [[nodiscard]] std::span<const template_id> events_on(map_id map) const;
    [[nodiscard]] std::size_t loot_owner_count() const noexcept { return loot_owner_links_; }

private:
    std::unordered_map<std::uint64_t, std::vector<template_id>> loot_owners_;
    std::unordered_map<map_id, std::vector<template_id>> spawns_by_map_;
    std::unordered_map<map_id, std::vector<template_id>> env_spawns_by_map_;
    std::unordered_map<map_id, std::vector<template_id>> events_by_map_;
    std::size_t loot_owner_links_{};
};

[[nodiscard]] game_data_store load_game_data(const std::filesystem::path& directory);
// Loads skills/items/npcs/monsters/spawns CSVs from a directory over an existing store,
// replacing matching IDs (the server's own rows on top of the client database).
void apply_game_data_overrides(const std::filesystem::path& directory, game_data_store& store);
void load_walk_meshes(const std::filesystem::path& directory, game_data_store& store);

} // namespace sema::data
