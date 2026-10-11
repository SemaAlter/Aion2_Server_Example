#include "sema/data/database.hpp"

#include <Windows.h>

#include <charconv>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace sema::data {
namespace {

struct MYSQL;
struct MYSQL_RES;
using MYSQL_ROW = char**;

class mysql_api {
public:
    explicit mysql_api(const std::filesystem::path& requested) {
        const auto path = requested.empty() ? std::filesystem::path("libmysql.dll") : requested;
        module_ = LoadLibraryW(path.c_str());
        if (!module_) throw std::runtime_error("could not load MySQL client library: " + path.string());
        init = symbol<decltype(init)>("mysql_init");
        real_connect = symbol<decltype(real_connect)>("mysql_real_connect");
        query = symbol<decltype(query)>("mysql_query");
        store_result = symbol<decltype(store_result)>("mysql_store_result");
        fetch_row = symbol<decltype(fetch_row)>("mysql_fetch_row");
        fetch_lengths = symbol<decltype(fetch_lengths)>("mysql_fetch_lengths");
        num_fields = symbol<decltype(num_fields)>("mysql_num_fields");
        free_result = symbol<decltype(free_result)>("mysql_free_result");
        error = symbol<decltype(error)>("mysql_error");
        close = symbol<decltype(close)>("mysql_close");
    }

    ~mysql_api() { if (module_) FreeLibrary(module_); }
    mysql_api(const mysql_api&) = delete;
    mysql_api& operator=(const mysql_api&) = delete;

    MYSQL* (__cdecl* init)(MYSQL*){};
    MYSQL* (__cdecl* real_connect)(MYSQL*, const char*, const char*, const char*, const char*, unsigned int,
        const char*, unsigned long){};
    int (__cdecl* query)(MYSQL*, const char*){};
    MYSQL_RES* (__cdecl* store_result)(MYSQL*){};
    MYSQL_ROW (__cdecl* fetch_row)(MYSQL_RES*){};
    unsigned long* (__cdecl* fetch_lengths)(MYSQL_RES*){};
    unsigned int (__cdecl* num_fields)(MYSQL_RES*){};
    void (__cdecl* free_result)(MYSQL_RES*){};
    const char* (__cdecl* error)(MYSQL*){};
    void (__cdecl* close)(MYSQL*){};

private:
    template <typename Function>
    Function symbol(const char* name) {
        const auto address = GetProcAddress(module_, name);
        if (!address) throw std::runtime_error(std::string("MySQL client is missing symbol: ") + name);
        return reinterpret_cast<Function>(address);
    }

    HMODULE module_{};
};

class mysql_connection {
public:
    mysql_connection(mysql_api& api, const database_config& config) : api_(api) {
        handle_ = api_.init(nullptr);
        if (!handle_) throw std::runtime_error("mysql_init failed");
        if (!api_.real_connect(handle_, config.host.c_str(), config.user.c_str(), config.password.c_str(),
                config.database.c_str(), config.port, nullptr, 0)) {
            const std::string message = api_.error(handle_);
            api_.close(handle_);
            handle_ = nullptr;
            throw std::runtime_error("MySQL connection failed: " + message);
        }
        execute("SET NAMES utf8mb4");
    }

    ~mysql_connection() { if (handle_) api_.close(handle_); }
    mysql_connection(const mysql_connection&) = delete;
    mysql_connection& operator=(const mysql_connection&) = delete;

    void execute(const char* sql) {
        if (api_.query(handle_, sql) != 0) throw std::runtime_error(std::string("MySQL query failed: ") + api_.error(handle_));
    }

    [[nodiscard]] std::vector<std::vector<std::string>> rows(const char* sql) {
        execute(sql);
        MYSQL_RES* result = api_.store_result(handle_);
        if (!result) throw std::runtime_error(std::string("MySQL result failed: ") + api_.error(handle_));
        struct result_guard {
            mysql_api& api;
            MYSQL_RES* value;
            ~result_guard() { api.free_result(value); }
        } guard{api_, result};

        const auto fields = api_.num_fields(result);
        std::vector<std::vector<std::string>> output;
        while (const auto row = api_.fetch_row(result)) {
            const auto lengths = api_.fetch_lengths(result);
            std::vector<std::string> values;
            values.reserve(fields);
            for (unsigned i = 0; i < fields; ++i) {
                values.emplace_back(row[i] ? std::string(row[i], lengths[i]) : std::string{});
            }
            output.push_back(std::move(values));
        }
        return output;
    }

private:
    mysql_api& api_;
    MYSQL* handle_{};
};

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

template <typename Integer>
Integer integer(const std::string& value, const char* field) {
    unsigned long long parsed{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size()
        || parsed > static_cast<unsigned long long>((std::numeric_limits<Integer>::max)())) {
        throw std::runtime_error(std::string("invalid database ") + field + ": " + value);
    }
    return static_cast<Integer>(parsed);
}

float number(const std::string& value, const char* field) {
    try {
        std::size_t end{};
        const auto parsed = std::stof(value, &end);
        if (end != value.size()) throw std::invalid_argument("trailing data");
        return parsed;
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("invalid database ") + field + ": " + value);
    }
}

faction parse_faction(const std::string& value) {
    if (value == "elyos") return faction::elyos;
    if (value == "asmodian") return faction::asmodian;
    if (value == "neutral") return faction::neutral;
    throw std::runtime_error("invalid database faction: " + value);
}

bool boolean(const std::string& value) { return value == "1" || value == "true"; }

template <typename Record>
Record& owner(catalog<Record>& records, const std::string& id, const char* what) {
    auto* record = records.find_mutable(integer<template_id>(id, what));
    if (!record) throw std::runtime_error(std::string("database ") + what + " references a missing record: " + id);
    return *record;
}

loot_owner_kind parse_owner_kind(const std::string& value) {
    if (value == "npc") return loot_owner_kind::npc;
    if (value == "env_object") return loot_owner_kind::env_object;
    if (value == "dungeon") return loot_owner_kind::dungeon;
    if (value == "item") return loot_owner_kind::item;
    if (value == "quest") return loot_owner_kind::quest;
    if (value == "field_event") return loot_owner_kind::field_event;
    return loot_owner_kind::other;
}

loot_link_source parse_link_source(const std::string& value) {
    if (value == "decompose") return loot_link_source::decompose;
    if (value == "level_scale") return loot_link_source::level_scale;
    return loot_link_source::package;
}

// Everything beyond the core catalogs: maps, NPC skills, skill effects, quest steps and rewards,
// event actions, the loot graph and its owners, and environment objects.
void load_world_extensions(mysql_connection& connection, game_data_store& result) {
    for (const auto& row : connection.rows("SELECT id,name FROM maps ORDER BY id")) {
        result.maps.add({integer<template_id>(row[0], "map ID"), row[1]});
    }
    for (const auto& row : connection.rows("SELECT npc_id,skill_id FROM npc_skills ORDER BY npc_id,priority")) {
        owner(result.npcs, row[0], "NPC skill").skills.push_back(integer<template_id>(row[1], "NPC skill ID"));
    }
    for (const auto& row : connection.rows(
             "SELECT skill_id,effect_index,effect_type,value_expression,IFNULL(target_filter_id,0) FROM skill_effects "
             "ORDER BY skill_id,effect_index")) {
        owner(result.skills, row[0], "skill effect").effects.push_back({integer<std::uint16_t>(row[1], "effect index"), row[2],
            row[3], integer<template_id>(row[4], "effect target filter")});
    }
    for (const auto& row : connection.rows(
             "SELECT quest_id,objective_index,objective_type,IFNULL(target_id,0),required_count,IFNULL(payload,'') "
             "FROM quest_objectives ORDER BY quest_id,objective_index")) {
        owner(result.quests, row[0], "quest objective").objectives.push_back({integer<std::uint16_t>(row[1], "objective index"),
            row[2], integer<template_id>(row[3], "objective target"), integer<std::uint32_t>(row[4], "objective count"), row[5]});
    }
    for (const auto& row : connection.rows(
             "SELECT quest_id,IFNULL(item_id,0),item_count,experience,currency FROM quest_rewards ORDER BY quest_id,reward_index")) {
        owner(result.quests, row[0], "quest reward").rewards.push_back({integer<template_id>(row[1], "reward item"),
            integer<std::uint32_t>(row[2], "reward count"), integer<std::uint64_t>(row[3], "reward experience"),
            integer<std::uint64_t>(row[4], "reward currency"), {}});
    }
    for (const auto& row : connection.rows(
             "SELECT quest_id,character_class,item_id,item_count FROM quest_class_rewards ORDER BY quest_id,character_class,reward_index")) {
        owner(result.quests, row[0], "class quest reward").rewards.push_back({integer<template_id>(row[2], "reward item"),
            integer<std::uint32_t>(row[3], "reward count"), 0, 0, row[1]});
    }
    for (const auto& row : connection.rows(
             "SELECT event_id,action_index,action_type,delay_ms,payload FROM world_event_actions ORDER BY event_id,action_index")) {
        owner(result.events, row[0], "event action").actions.push_back({integer<std::uint16_t>(row[1], "action index"), row[2],
            integer<std::uint32_t>(row[3], "action delay"), row[4]});
    }
    for (const auto& row : connection.rows("SELECT id,name FROM loot_tables ORDER BY id")) {
        result.loot_tables.add({integer<template_id>(row[0], "loot table ID"), row[1], {}, {}});
    }
    for (const auto& row : connection.rows(
             "SELECT loot_table_id,item_id,minimum_count,maximum_count,chance FROM loot_table_entries")) {
        owner(result.loot_tables, row[0], "loot entry").entries.push_back({integer<template_id>(row[1], "loot item"),
            integer<std::uint32_t>(row[2], "loot minimum"), integer<std::uint32_t>(row[3], "loot maximum"),
            number(row[4], "loot chance")});
    }
    for (const auto& row : connection.rows(
             "SELECT parent_loot_table_id,child_loot_table_id,chance,item_count,level_min,level_max,link_source FROM loot_table_links")) {
        owner(result.loot_tables, row[0], "loot link").links.push_back({integer<template_id>(row[1], "loot child"),
            number(row[2], "loot link chance"), integer<std::uint32_t>(row[3], "loot link count"),
            integer<std::uint16_t>(row[4], "loot level min"), integer<std::uint16_t>(row[5], "loot level max"),
            parse_link_source(row[6])});
    }
    for (const auto& row : connection.rows(
             "SELECT owner_kind,owner_id,loot_table_id FROM loot_owners WHERE owner_id IS NOT NULL AND owner_id <= 4294967295")) {
        result.add_loot_owner(parse_owner_kind(row[0]), integer<template_id>(row[1], "loot owner"),
            integer<template_id>(row[2], "owned loot table"));
    }
    for (const auto& row : connection.rows(
             "SELECT id,name,usage_type,usage_value,interact_range,interact_time_ms FROM env_objects ORDER BY id")) {
        result.env_objects.add({integer<template_id>(row[0], "env object ID"), row[1], row[2], row[3],
            integer<std::uint32_t>(row[4], "interact range"), integer<std::uint32_t>(row[5], "interact time")});
    }
    for (const auto& row : connection.rows("SELECT id,env_object_id,map_id,x,y,z,heading FROM env_object_spawns ORDER BY id")) {
        result.env_object_spawns.add({integer<template_id>(row[0], "env spawn ID"), integer<template_id>(row[1], "env object"),
            integer<map_id>(row[2], "env spawn map"), {number(row[3], "env x"), number(row[4], "env y"), number(row[5], "env z")},
            number(row[6], "env heading")});
    }
}

} // namespace

database_config load_database_config(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("could not open database config: " + path.string());
    database_config result;
    std::string line;
    bool in_database_section = false;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            in_database_section = line == "[database]";
            continue;
        }
        if (!in_database_section) continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) throw std::runtime_error("invalid database config line: " + line);
        const auto key = trim(line.substr(0, equals));
        const auto value = trim(line.substr(equals + 1));
        if (key == "host") result.host = value;
        else if (key == "port") result.port = integer<unsigned short>(value, "port");
        else if (key == "user") result.user = value;
        else if (key == "password") result.password = value;
        else if (key == "database") result.database = value;
        else if (key == "client_library") result.client_library = value;
        else throw std::runtime_error("unknown database config key: " + key);
    }
    if (result.user.empty() || result.database.empty()) throw std::runtime_error("database config requires user and database");
    return result;
}

game_data_store load_game_data_mysql(const database_config& config, const std::filesystem::path& walk_mesh_directory) {
    mysql_api api(config.client_library);
    mysql_connection connection(api, config);
    game_data_store result;

    for (const auto& row : connection.rows("SELECT id,name,max_level,cooldown_ms,cast_time_ms,skill_range,target,hit_shape,max_targets,angle_degrees FROM skills ORDER BY id")) {
        skill_target target = skill_target::hostile;
        if (row[6] == "self") target = skill_target::self;
        else if (row[6] == "friendly") target = skill_target::friendly;
        else if (row[6] == "ground") target = skill_target::ground;
        skill_hit_shape shape = skill_hit_shape::single;
        if (row[7] == "radius") shape = skill_hit_shape::radius;
        else if (row[7] == "frontal_cone") shape = skill_hit_shape::frontal_cone;
        result.skills.add({integer<template_id>(row[0], "skill ID"), row[1], integer<std::uint16_t>(row[2], "skill level"),
            integer<std::uint32_t>(row[3], "skill cooldown"), integer<std::uint32_t>(row[4], "skill cast time"),
            number(row[5], "skill range"), target, shape, integer<std::uint16_t>(row[8], "skill max targets"),
            number(row[9], "skill angle")});
    }
    for (const auto& row : connection.rows("SELECT id,name,category,max_stack,required_level FROM items ORDER BY id")) {
        item_category category = item_category::unknown;
        if (row[2] == "weapon") category = item_category::weapon;
        else if (row[2] == "armor") category = item_category::armor;
        else if (row[2] == "consumable") category = item_category::consumable;
        else if (row[2] == "quest") category = item_category::quest;
        else if (row[2] == "material") category = item_category::material;
        result.items.add({integer<template_id>(row[0], "item ID"), row[1], category,
            integer<std::uint32_t>(row[3], "item max stack"), integer<std::uint16_t>(row[4], "item required level")});
    }
    for (const auto& row : connection.rows("SELECT id,name,kind,level,faction,interaction FROM npcs ORDER BY id")) {
        npc_kind kind = npc_kind::ambient;
        if (row[2] == "merchant") kind = npc_kind::merchant;
        else if (row[2] == "quest") kind = npc_kind::quest;
        else if (row[2] == "teleporter") kind = npc_kind::teleporter;
        else if (row[2] == "monster") kind = npc_kind::monster;
        result.npcs.add({integer<template_id>(row[0], "NPC ID"), row[1], kind,
            integer<std::uint16_t>(row[3], "NPC level"), parse_faction(row[4]), row[5]});
    }
    for (const auto& row : connection.rows("SELECT id,npc_id,monster_rank,max_health,attack_value,defense_value,aggro_radius,move_speed,attack_range,server_skill_id,attack_interval_ms FROM monsters ORDER BY id")) {
        monster_rank rank = monster_rank::normal;
        if (row[2] == "elite") rank = monster_rank::elite;
        else if (row[2] == "boss") rank = monster_rank::boss;
        result.monsters.add({integer<template_id>(row[0], "monster ID"), integer<template_id>(row[1], "monster NPC ID"), rank,
            integer<std::uint64_t>(row[3], "monster health"), integer<std::uint32_t>(row[4], "monster attack"),
            integer<std::uint32_t>(row[5], "monster defense"), number(row[6], "monster aggro radius"),
            number(row[7], "monster move speed"), number(row[8], "monster attack range"),
            integer<std::uint32_t>(row[9], "monster skill"), integer<std::uint32_t>(row[10], "monster attack interval")});
    }
    for (const auto& row : connection.rows("SELECT id,template_id,kind,map_id,x,y,z,heading,respawn_seconds FROM spawns ORDER BY id")) {
        result.spawns.add({integer<template_id>(row[0], "spawn ID"), integer<template_id>(row[1], "spawn template ID"),
            row[2] == "monster" ? spawn_kind::monster : spawn_kind::npc, integer<map_id>(row[3], "spawn map ID"),
            {number(row[4], "spawn x"), number(row[5], "spawn y"), number(row[6], "spawn z")},
            number(row[7], "spawn heading"), integer<std::uint32_t>(row[8], "spawn respawn")});
    }
    for (const auto& row : connection.rows("SELECT id,name,minimum_level,faction,repeatable FROM quests ORDER BY id")) {
        result.quests.add({integer<template_id>(row[0], "quest ID"), row[1], integer<std::uint16_t>(row[2], "quest level"),
            parse_faction(row[3]), boolean(row[4])});
    }
    for (const auto& row : connection.rows("SELECT id,name,map_id,trigger_type,trigger_value,enabled FROM world_events ORDER BY id")) {
        result.events.add({integer<template_id>(row[0], "event ID"), row[1], integer<map_id>(row[2], "event map ID"),
            row[3], row[4], boolean(row[5])});
    }
    load_world_extensions(connection, result);
    const auto table_count = connection.rows("SELECT COUNT(*) FROM client_data_tables");
    if (!table_count.empty()) result.indexed_client_tables = integer<std::size_t>(table_count.front().front(), "client data table count");
    if (!walk_mesh_directory.empty()) load_walk_meshes(walk_mesh_directory, result);
    result.build_indexes();
    result.validate();
    return result;
}

} // namespace sema::data
