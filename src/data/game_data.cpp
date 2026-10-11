#include "sema/data/game_data.hpp"

#include "csv_reader.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace sema::data {
namespace {

template <typename Enum>
Enum parse_enum(const std::string& value, const std::initializer_list<std::pair<std::string_view, Enum>>& values,
    const char* field) {
    for (const auto& [name, item] : values) if (value == name) return item;
    throw std::runtime_error(std::string("invalid ") + field + ": " + value);
}

std::uint16_t u16(const std::string& value, const char* field) {
    const auto parsed = detail::u32(value, field);
    if (parsed > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error(std::string(field) + " exceeds 16 bits");
    }
    return static_cast<std::uint16_t>(parsed);
}

faction parse_faction(const std::string& value) {
    return parse_enum<faction>(value, {{"neutral", faction::neutral}, {"elyos", faction::elyos},
        {"asmodian", faction::asmodian}}, "faction");
}

void load_skills(const std::filesystem::path& path, catalog<skill_template>& output, const bool replace = false) {
    for (const auto& row : detail::read_csv(path,
        {"id", "name", "max_level", "cooldown_ms", "cast_time_ms", "range", "target",
            "hit_shape", "max_targets", "angle_degrees"})) {
        skill_template record{detail::u32(row.fields[0], "skill ID"), row.fields[1], u16(row.fields[2], "max level"),
            detail::u32(row.fields[3], "cooldown"), detail::u32(row.fields[4], "cast time"),
            detail::number(row.fields[5], "range"),
            parse_enum<skill_target>(row.fields[6], {{"self", skill_target::self},
                {"friendly", skill_target::friendly}, {"hostile", skill_target::hostile},
                {"ground", skill_target::ground}}, "skill target"),
            parse_enum<skill_hit_shape>(row.fields[7], {{"single", skill_hit_shape::single},
                {"radius", skill_hit_shape::radius}, {"frontal_cone", skill_hit_shape::frontal_cone}},
                "skill hit shape"),
            u16(row.fields[8], "skill max targets"), detail::number(row.fields[9], "skill angle")};
        if (replace) output.add_or_replace(std::move(record)); else output.add(std::move(record));
    }
}

void load_items(const std::filesystem::path& path, catalog<item_template>& output, const bool replace = false) {
    const auto put = [&](item_template record) {
        if (replace) output.add_or_replace(std::move(record)); else output.add(std::move(record));
    };
    for (const auto& row : detail::read_csv(path,
        {"id", "name", "category", "max_stack", "required_level"})) {
        put({detail::u32(row.fields[0], "item ID"), row.fields[1],
            parse_enum<item_category>(row.fields[2], {{"unknown", item_category::unknown},
                {"weapon", item_category::weapon}, {"armor", item_category::armor},
                {"consumable", item_category::consumable}, {"quest", item_category::quest},
                {"material", item_category::material}}, "item category"),
            detail::u32(row.fields[3], "max stack"), u16(row.fields[4], "required level")});
    }
}

void load_npcs(const std::filesystem::path& path, catalog<npc_template>& output, const bool replace = false) {
    const auto put = [&](npc_template record) {
        if (replace) output.add_or_replace(std::move(record)); else output.add(std::move(record));
    };
    for (const auto& row : detail::read_csv(path,
        {"id", "name", "kind", "level", "faction", "interaction"})) {
        put({detail::u32(row.fields[0], "NPC ID"), row.fields[1],
            parse_enum<npc_kind>(row.fields[2], {{"ambient", npc_kind::ambient},
                {"merchant", npc_kind::merchant}, {"quest", npc_kind::quest},
                {"teleporter", npc_kind::teleporter}, {"monster", npc_kind::monster}}, "NPC kind"),
            u16(row.fields[3], "NPC level"), parse_faction(row.fields[4]), row.fields[5]});
    }
}

void load_monsters(const std::filesystem::path& path, catalog<monster_template>& output, const bool replace = false) {
    const auto put = [&](monster_template record) {
        if (replace) output.add_or_replace(std::move(record)); else output.add(std::move(record));
    };
    for (const auto& row : detail::read_csv(path,
        {"id", "npc_id", "rank", "max_health", "attack", "defense", "aggro_radius", "move_speed",
            "attack_range", "server_skill_id", "attack_interval_ms"})) {
        put({detail::u32(row.fields[0], "monster ID"), detail::u32(row.fields[1], "NPC ID"),
            parse_enum<monster_rank>(row.fields[2], {{"normal", monster_rank::normal},
                {"elite", monster_rank::elite}, {"boss", monster_rank::boss}}, "monster rank"),
            detail::u64(row.fields[3], "max health"), detail::u32(row.fields[4], "attack"),
            detail::u32(row.fields[5], "defense"), detail::number(row.fields[6], "aggro radius"),
            detail::number(row.fields[7], "move speed"), detail::number(row.fields[8], "attack range"),
            detail::u32(row.fields[9], "monster server skill ID"),
            detail::u32(row.fields[10], "monster attack interval")});
    }
}

void load_spawns(const std::filesystem::path& path, catalog<spawn_point>& output, const bool replace = false) {
    const auto put = [&](spawn_point record) {
        if (replace) output.add_or_replace(std::move(record)); else output.add(std::move(record));
    };
    for (const auto& row : detail::read_csv(path,
        {"id", "template_id", "kind", "map_id", "x", "y", "z", "heading", "respawn_seconds"})) {
        put({detail::u32(row.fields[0], "spawn ID"), detail::u32(row.fields[1], "template ID"),
            parse_enum<spawn_kind>(row.fields[2], {{"npc", spawn_kind::npc},
                {"monster", spawn_kind::monster}}, "spawn kind"),
            detail::u32(row.fields[3], "map ID"), {detail::number(row.fields[4], "x"),
                detail::number(row.fields[5], "y"), detail::number(row.fields[6], "z")},
            detail::number(row.fields[7], "heading"), detail::u32(row.fields[8], "respawn seconds")});
    }
}

} // namespace

void game_data_store::validate() const {
    for (const auto& [id, monster] : monsters.records()) {
        const auto* npc = npcs.find(monster.npc_id);
        if (!npc) throw std::runtime_error("monster " + std::to_string(id) + " references unknown NPC");
        if (npc->kind != npc_kind::monster) {
            throw std::runtime_error("monster " + std::to_string(id) + " references a non-monster NPC");
        }
        if (monster.max_health == 0) throw std::runtime_error("monster health cannot be zero");
        if (monster.move_speed <= 0.0F || monster.attack_range <= 0.0F
            || monster.server_skill_id == 0 || monster.attack_interval_ms == 0) {
            throw std::runtime_error("monster combat data cannot be zero");
        }
    }
    for (const auto& [id, spawn] : spawns.records()) {
        const bool exists = spawn.kind == spawn_kind::monster
            ? monsters.find(spawn.template_ref) != nullptr
            : npcs.find(spawn.template_ref) != nullptr;
        if (!exists) throw std::runtime_error("spawn " + std::to_string(id) + " references unknown template");
    }
    for (const auto& [id, npc] : npcs.records()) {
        for (const auto skill : npc.skills) {
            if (!skills.find(skill)) throw std::runtime_error("NPC " + std::to_string(id) + " references unknown skill");
        }
    }
    for (const auto& [id, table] : loot_tables.records()) {
        for (const auto& entry : table.entries) {
            if (!items.find(entry.item_id)) throw std::runtime_error("loot table " + std::to_string(id) + " references unknown item");
        }
        for (const auto& link : table.links) {
            if (!loot_tables.find(link.child_table)) {
                throw std::runtime_error("loot table " + std::to_string(id) + " links an unknown table");
            }
        }
    }
    for (const auto& [key, tables] : loot_owners_) {
        for (const auto table : tables) {
            if (!loot_tables.find(table)) throw std::runtime_error("loot owner references unknown table");
        }
    }
    for (const auto& [id, spawn] : env_object_spawns.records()) {
        if (!env_objects.find(spawn.env_object_id)) {
            throw std::runtime_error("environment spawn " + std::to_string(id) + " references unknown object");
        }
    }
}

namespace {
std::uint64_t owner_key(const loot_owner_kind kind, const template_id owner) {
    return (static_cast<std::uint64_t>(kind) << 32) | owner;
}

std::span<const template_id> lookup(const std::unordered_map<map_id, std::vector<template_id>>& index, const map_id key) {
    const auto found = index.find(key);
    return found == index.end() ? std::span<const template_id>{} : std::span<const template_id>(found->second);
}
} // namespace

void game_data_store::add_loot_owner(const loot_owner_kind kind, const template_id owner, const template_id table) {
    auto& tables = loot_owners_[owner_key(kind, owner)];
    if (std::ranges::find(tables, table) == tables.end()) {
        tables.push_back(table);
        ++loot_owner_links_;
    }
}

std::span<const template_id> game_data_store::loot_tables_for(const loot_owner_kind kind, const template_id owner) const {
    const auto found = loot_owners_.find(owner_key(kind, owner));
    return found == loot_owners_.end() ? std::span<const template_id>{} : std::span<const template_id>(found->second);
}

void game_data_store::build_indexes() {
    spawns_by_map_.clear();
    env_spawns_by_map_.clear();
    events_by_map_.clear();
    for (const auto& [id, spawn] : spawns.records()) spawns_by_map_[spawn.map].push_back(id);
    for (const auto& [id, spawn] : env_object_spawns.records()) env_spawns_by_map_[spawn.map].push_back(id);
    for (const auto& [id, event] : events.records()) events_by_map_[event.map].push_back(id);
    // Stable order so world creation (and entity IDs derived from it) is deterministic.
    for (auto* index : {&spawns_by_map_, &env_spawns_by_map_, &events_by_map_}) {
        for (auto& [map, ids] : *index) std::ranges::sort(ids);
    }
}

std::span<const template_id> game_data_store::spawns_on(const map_id map) const { return lookup(spawns_by_map_, map); }
std::span<const template_id> game_data_store::env_object_spawns_on(const map_id map) const {
    return lookup(env_spawns_by_map_, map);
}
std::span<const template_id> game_data_store::events_on(const map_id map) const { return lookup(events_by_map_, map); }

void load_walk_meshes(const std::filesystem::path& directory, game_data_store& result) {
    const auto mesh_directory = directory / "walk-meshes";
    if (!std::filesystem::is_directory(mesh_directory)) return;
    for (const auto& entry : std::filesystem::directory_iterator(mesh_directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".obj") continue;
        const auto map = detail::u32(entry.path().stem().string(), "walk-mesh map ID");
        if (!result.walk_meshes.emplace(map, load_walk_mesh_obj(entry.path())).second) {
            throw std::runtime_error("duplicate walk mesh for map " + std::to_string(map));
        }
    }
}

void apply_game_data_overrides(const std::filesystem::path& directory, game_data_store& store) {
    // Server-owned rows (config/game-data) replace the client catalogs' matching IDs.
    if (!std::filesystem::is_directory(directory)) return;
    if (const auto path = directory / "skills.csv"; std::filesystem::is_regular_file(path)) load_skills(path, store.skills, true);
    if (const auto path = directory / "items.csv"; std::filesystem::is_regular_file(path)) load_items(path, store.items, true);
    if (const auto path = directory / "npcs.csv"; std::filesystem::is_regular_file(path)) load_npcs(path, store.npcs, true);
    if (const auto path = directory / "monsters.csv"; std::filesystem::is_regular_file(path)) load_monsters(path, store.monsters, true);
    if (const auto path = directory / "spawns.csv"; std::filesystem::is_regular_file(path)) load_spawns(path, store.spawns, true);
    store.build_indexes();
    store.validate();
}

game_data_store load_game_data(const std::filesystem::path& directory) {
    if (!std::filesystem::is_directory(directory)) {
        throw std::runtime_error("game-data directory does not exist: " + directory.string());
    }
    game_data_store result;
    const auto generated_directory = directory / "generated";
    const auto generated_skills = generated_directory / "skills.csv";
    if (std::filesystem::is_regular_file(generated_skills)) {
        load_skills(generated_skills, result.skills);
    }
    load_skills(directory / "skills.csv", result.skills, true);
    load_items(directory / "items.csv", result.items);
    load_npcs(directory / "npcs.csv", result.npcs);
    load_monsters(directory / "monsters.csv", result.monsters);
    load_spawns(directory / "spawns.csv", result.spawns);

    load_walk_meshes(directory, result);
    result.build_indexes();
    result.validate();
    return result;
}

} // namespace sema::data
